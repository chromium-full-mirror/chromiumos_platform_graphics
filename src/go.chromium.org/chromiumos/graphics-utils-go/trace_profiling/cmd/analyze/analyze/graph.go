// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package analyze

import (
	"fmt"
	"image/color"
	"math"
	"os/exec"
	"reflect"

	"golang.org/x/image/colornames"
	"gonum.org/v1/plot"
	"gonum.org/v1/plot/plotter"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
)

const plotFileName = "./analyzer_calls.png"

// PlotCallNameUsagePerFrame generates a scatter-plot of the number of calls
// to GL functions that match the given regex per frame and launches Chrome
// to show the resulting graph.
func PlotCallNameUsagePerFrame(prof *ProfileData, callNameRegex string) error {
	var calls []int
	var err error
	if calls, err = GatherNumCallsPerFrame(prof, callNameRegex); err != nil {
		return err
	}

	// Selector function, used with createScatterPlot, extract value from array
	// entry. Trivial in this case, since each entry is an int.
	var sel = func(v interface{}) float64 {
		var f = v.(int)
		return float64(f)
	}
	var s *plotter.Scatter
	s, err = createScatterPlot(calls, sel, 1, colornames.Blue, draw.PlusGlyph{})

	var p *plot.Plot = plot.New()

	p.Title.Text = fmt.Sprintf("Calls to %s per frame", callNameRegex)
	p.X.Label.Text = "frame num"
	p.Y.Label.Text = "call count"
	p.Add(plotter.NewGrid())

	p.Add(s)
	return showPlot(p)
}

// PlotFrameTime creates and displays a graph of the time spent either in the
// CPU or GPU for the profiles prof1 and prof2. Both profiles are shown in the
// same graph, prof1 in blue and prof2 in red. Either profile may be omitted by
// setting it to nil. Parameter plotType selects either a "CPU" plot or a
// "GPU" plot.
func PlotFrameTime(plotType string, prof1 *ProfileData, prof2 *ProfileData) error {
	var err error
	var frameTiming1, frameTiming2 []FrameTiming
	var s1, s2 *plotter.Scatter

	if prof1 != nil {
		frameTiming1 = GatherTimingForAllFrames(prof1)
	}
	if prof2 != nil {
		frameTiming2 = GatherTimingForAllFrames(prof2)
	}

	// A valueSelector function is used to extract either the CPU or GPU timing
	// value in the generic functions findMaxValueInArray and createScatterPlot below.
	var selCPU = func(v interface{}) float64 {
		var f = v.(FrameTiming)
		return float64(f.cpuTimeNs)
	}
	var selGPU = func(v interface{}) float64 {
		var f = v.(FrameTiming)
		return float64(f.gpuTimeNs)
	}
	var label = "CPU"
	var plotSelector = selCPU
	if plotType == "GPU" {
		plotSelector = selGPU
		label = "GPU"
	}

	// Find the max duration value so we can properly scale the graph.
	maxDuration := math.Max(findMaxValueInArray(frameTiming1, plotSelector),
		findMaxValueInArray(frameTiming2, plotSelector))

	scale := 0.001
	units := "uS"
	if maxDuration > 10000000.0 {
		scale = 1e-6
		units = "mS"
	}

	// Now we're ready to create and display the graphs.
	if prof1 != nil {
		s1, err = createScatterPlot(
			frameTiming1, plotSelector, scale, colornames.Blue, draw.PlusGlyph{})
		if err != nil {
			return err
		}
	}

	if prof2 != nil {
		s2, err = createScatterPlot(
			frameTiming2, plotSelector, scale, colornames.Red, draw.CrossGlyph{})
		if err != nil {
			return err
		}
	}

	var p *plot.Plot = plot.New()

	p.Title.Text = fmt.Sprintf("Frame duration")
	p.X.Label.Text = "frame num"
	p.Y.Label.Text = label + " duration in " + units
	p.Add(plotter.NewGrid())

	if s1 != nil {
		p.Add(s1)
	}
	if s2 != nil {
		p.Add(s2)
	}
	return showPlot(p)
}

// Create a scatter plot out of data taken from an array of values. Function
// sel is used to extract the candidate value from each array entry.
func createScatterPlot(
	values interface{},
	sel func(interface{}) float64,
	scale float64,
	color color.Color,
	shape draw.GlyphDrawer) (*plotter.Scatter, error) {

	valuesArr := reflect.ValueOf(values)
	pts := make(plotter.XYs, valuesArr.Len())
	for i := 0; i < valuesArr.Len(); i++ {
		v := valuesArr.Index(i)
		pts[i].X = float64(i)
		pts[i].Y = float64(sel(v.Interface())) * scale
	}

	var err error
	var s *plotter.Scatter
	if s, err = plotter.NewScatter(pts); err != nil {
		return nil, err
	}

	s.GlyphStyle.Color = color
	s.Shape = shape

	return s, nil
}

// Find and return the maximum value in an array of values, with function
// sel used to extract the candidate value from each array antry.
func findMaxValueInArray(values interface{}, sel func(interface{}) float64) float64 {
	if values == nil {
		return 0
	}

	valuesArr := reflect.ValueOf(values)
	if valuesArr.Len() == 0 {
		return 0
	}

	var max = sel(valuesArr.Index(0).Interface())
	for i := 1; i < valuesArr.Len(); i++ {
		v := sel(valuesArr.Index(i).Interface())
		max = math.Max(max, v)
	}
	return max
}

func showPlot(p *plot.Plot) error {
	var err error

	// Save the plot to a PNG file.
	if err = p.Save(9*vg.Inch, 4*vg.Inch, plotFileName); err != nil {
		return err
	}

	// Show plot. If 'display' is available (imagemagick), we'll use that.
	// Otherwise we fallback to Chrome.
	var cmd *exec.Cmd
	_, err = exec.LookPath("display")
	if err != nil {
		cmd = exec.Command("/usr/bin/google-chrome", plotFileName)
	} else {
		cmd = exec.Command("display", plotFileName)
	}
	return cmd.Start()
}
