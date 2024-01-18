// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"github.com/pkg/errors"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

type PortagePackage struct {
	Name     string `json:"Name"`
	Version  string `json:"Version"`
	Revision string `json:"Revision,omitempty"`
}

// queryPortageDB tries to find nameGlob in portage DB.
// If multiple packages are found in nameGlob, it passes to resolver to decide.
func queryPortageDB(nameGlob string, resolver func([]string) (string, error)) (PortagePackage, error) {
	glob := filepath.Join("/var/db/pkg", nameGlob)
	packages, err := filepath.Glob(glob)
	if err != nil {
		return PortagePackage{}, errors.Wrapf(err, "failed to glob %v", nameGlob)
	}
	targetPackage, err := resolver(packages)
	if err != nil {
		return PortagePackage{}, errors.Wrap(err, "failed to determine packages")
	}

	// targetPackage is in the form of /var/db/pkg/{PackageName}-{Version}-r{Revision}
	re := regexp.MustCompile(`\/var\/db\/pkg\/(\S+)-([\d+\.]+)(-r(\d+))?`)
	match := re.FindStringSubmatch(targetPackage)
	if match == nil {
		return PortagePackage{}, errors.Errorf("failed to parse portage package information: %v", targetPackage)
	}

	result := PortagePackage{Name: match[1], Version: match[2]}
	if len(match) > 3 {
		result.Revision = match[4]
	}
	return result, nil
}

// extractOpenGLVersion takes the output of the glxinfo/wflinfo command and attempts to extract the OpenGL version.
func extractOpenGLVersion() (Version, error) {
	// OpenGL ES profile version string: OpenGL ES 3.2 Mesa 23.2.1-1+build1
	m, n, glxInfoErr := func() (major int, minor int, err error) {
		glxinfo, err := getGlxinfo()
		if err != nil {
			return 0, 0, errors.Wrap(err, "failed to run glxinfo")
		}
		re := regexp.MustCompile(`OpenGL ES profile version string: OpenGL ES ([0-9]+)\.([0-9]+)`)
		matches := re.FindAllStringSubmatch(glxinfo, -1)
		if len(matches) != 1 {
			return 0, 0, errors.Errorf("%d OpenGL version string found in glxinfo output: %v", len(matches), glxinfo)
		}
		if major, err = strconv.Atoi(matches[0][1]); err != nil {
			return 0, 0, errors.Wrap(err, "could not parse major version")
		}
		if minor, err = strconv.Atoi(matches[0][2]); err != nil {
			return 0, 0, errors.Wrap(err, "could not parse minor version")
		}
		return major, minor, err
	}()
	if glxInfoErr == nil {
		return Version{[]int{m, n}}, nil
	}

	// OpenGL version string: OpenGL ES 3.2 Mesa 18.1.0-devel (git-131e871385)
	m, n, waffleInfoErr := func() (major int, minor int, err error) {
		wflout, err := getWaffleInfo()
		if err != nil {
			return 0, 0, errors.Wrap(err, "failed to run waffle")
		}
		re := regexp.MustCompile(`OpenGL version string: OpenGL ES ([0-9]+).([0-9]+)`)
		matches := re.FindAllStringSubmatch(wflout, -1)
		if len(matches) != 1 {
			return 0, 0, errors.Errorf("%d OpenGL version string found in wflinfo output: %v", len(matches), wflout)
		}
		if major, err = strconv.Atoi(matches[0][1]); err != nil {
			return 0, 0, errors.Wrap(err, "could not parse major version")
		}
		if minor, err = strconv.Atoi(matches[0][2]); err != nil {
			return 0, 0, errors.Wrap(err, "could not parse minor version")
		}
		return major, minor, err
	}()
	if waffleInfoErr == nil {
		return Version{[]int{m, n}}, nil
	}
	return Version{}, errors.Wrap(waffleInfoErr, errors.Wrap(glxInfoErr, "failed to determine OpenGL version").Error())
}

func getGLESVersion() (*Version, error) {
	version, err := extractOpenGLVersion()
	if err != nil {
		return nil, errors.Wrap(err, "failed to extract OpenGLES version")
	}
	return &version, nil
}

func getGLESDriverPackage() (*PortagePackage, error) {
	resolver := func(packages []string) (string, error) {
		// Returns the one that installs libEGL.so.
		for _, p := range packages {
			// CONTENTS file under each entry contains the installed objs.
			data, err := os.ReadFile(filepath.Join(p, "CONTENTS"))
			if err != nil {
				continue
			}
			for _, library := range []string{"libEGL.so", "libEGL_mesa.so", "libglapi.so"} {
				if strings.Contains(string(data), library) {
					return p, nil
				}
			}
		}
		return "", errors.Errorf("None of the packages are installing libEGL.so")
	}

	// Search for all possible package in ChromeOS that provide GLES driver.
	for _, globPattern := range []string{
		"media-libs/mesa-*",
		"media-libs/mali-drivers-*",
	} {
		portagePackage, err := queryPortageDB(globPattern, resolver)
		if err == nil {
			return &portagePackage, nil
		}
	}
	return nil, errors.Errorf("failed to find GL driver portage package")
}

// getVulkanVersion inspect the output of vulkaninfo and return relevant information.
func getVulkanVersion() (*Version, *Version, error) {
	output, err := exec.Command("vulkaninfo", "--summary").Output()
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to run vulkaninfo")
	}

	parseVersion := func(reg string) (Version, error) {
		match := regexp.MustCompile(fmt.Sprintf(`(?m)%v`, reg)).FindAllStringSubmatch(string(output), -1)
		if match == nil {
			return Version{}, errors.Wrapf(err, "failed to find %v from vulkaninfo", reg)
		}
		version, err := NewVersion(match[0][1])
		if err != nil {
			return Version{}, errors.Wrapf(err, "failed to convert %v to version", match[0][1])
		}
		return version, nil
	}

	instanceVersion, err := parseVersion(`^Vulkan Instance Version:\s+([\d\.]+)`)
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to parse instance version")
	}
	apiVersion, err := parseVersion(`^\s+apiVersion\s+=\s+([\d\.]+)`)
	if err != nil {
		return nil, nil, errors.Wrapf(err, "failed to parse api version")
	}
	return &instanceVersion, &apiVersion, nil
}

func getVulkanDriverPackage() (*PortagePackage, error) {
	resolver := func(packages []string) (string, error) {
		// Returns the one that installs files under vulkan/icd.d
		for _, p := range packages {
			// CONTENTS file under each entry contains the installed objs.
			data, err := os.ReadFile(filepath.Join(p, "CONTENTS"))
			if err != nil {
				continue
			}
			if strings.Contains(string(data), "/usr/share/vulkan/icd.d") {
				return p, nil
			}
		}
		return "", errors.Errorf("None of the packages are installing files in /usr/share/vulkan/icd.d")
	}

	// Search for all possible package in ChromeOS that may provide vulkan driver.
	for _, globPattern := range []string{
		"media-libs/mesa-*",
		"media-libs/mali-drivers-*",
	} {
		portagePackage, err := queryPortageDB(globPattern, resolver)
		if err == nil {
			return &portagePackage, nil
		}
	}
	return nil, errors.Errorf("failed to find GL driver portage package")
}

func getClvkDriverPackage() (*PortagePackage, error) {
	resolver := func(packages []string) (string, error) {
		// Returns the one that installs libOpenCL.so.
		for _, p := range packages {
			// CONTENTS file under each entry contains the installed objs.
			data, err := os.ReadFile(filepath.Join(p, "CONTENTS"))
			if err != nil {
				continue
			}
			if strings.Contains(string(data), "libOpenCL.so") {
				return p, nil
			}
		}
		return "", errors.Errorf("None of the packages are installing libOpenCL.so")
	}

	// Search for all possible package in ChromeOS that may provide clvk.
	for _, globPattern := range []string{
		"media-libs/clvk*",
	} {
		portagePackage, err := queryPortageDB(globPattern, resolver)
		if err == nil {
			return &portagePackage, nil
		}
	}
	return nil, errors.Errorf("failed to find Clvk portage package")
}
