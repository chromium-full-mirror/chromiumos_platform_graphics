// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package analyze

import (
	"container/heap"
	"fmt"
	"log"
)

// FloatHeap is a heap data structure storing float64. It implements the
// golang heap.interface. It is configurable so that either the min or max value
// percolates to the top of the heap.
type FloatHeap struct {
	values   []float64
	minOnTop bool
}

// Len implements heap.interface.
func (h *FloatHeap) Len() int { return len(h.values) }

// Less implements heap.interface.
func (h *FloatHeap) Less(i, j int) bool {
	if h.minOnTop {
		return h.values[i] < h.values[j]
	}
	return h.values[i] > h.values[j]
}

// Swap implements heap.interface.
func (h *FloatHeap) Swap(i, j int) {
	h.values[i], h.values[j] = h.values[j], h.values[i]
}

// Push implements heap.interface.
func (h *FloatHeap) Push(x interface{}) {
	h.values = append(h.values, x.(float64))
}

// Pop implements heap.interface.
func (h *FloatHeap) Pop() interface{} {
	n := len(h.values)
	x := h.values[n-1]
	h.values = h.values[0 : n-1]
	return x
}

// Peek returns the value at the top of the heap without removing it.
func (h *FloatHeap) Peek() float64 {
	return h.values[0]
}

// MedianTracker keeps track of the median of a ongoing series of float64 values.
// Run times are O(log N) insertion and O(1) median access.
type MedianTracker struct {
	// Two heaps such that all values in the min heap are <= the current median and
	// values in max heap are >= current median. Both heaps never differ by more
	// than one in size.
	minHeap FloatHeap
	maxHeap FloatHeap
}

// CreateMedianTracker creates a new MedianTracker, inserts initial value v1
// and returns a pointer to the instance.
func CreateMedianTracker(v1 float64) *MedianTracker {
	mt := MedianTracker{
		minHeap: FloatHeap{
			values:   make([]float64, 0),
			minOnTop: false,
		},
		maxHeap: FloatHeap{
			values:   make([]float64, 0),
			minOnTop: true,
		},
	}

	heap.Init(&mt.maxHeap)
	heap.Init(&mt.minHeap)

	mt.Add(v1)
	return &mt
}

// Add adds value v to the tracker.
func (mt *MedianTracker) Add(v float64) {
	if mt.minHeap.Len() == 0 {
		if mt.maxHeap.Len() == 0 || v > mt.maxHeap.Peek() {
			mt.addToMaxHeap(v)
		} else {
			mt.addToMinHeap(v)
		}
	} else if mt.maxHeap.Len() == 0 {
		if v < mt.minHeap.Peek() {
			mt.addToMinHeap(v)
		} else {
			mt.addToMaxHeap(v)
		}
	} else {
		m := mt.Median()
		if v > m {
			mt.addToMaxHeap(v)
		} else {
			mt.addToMinHeap(v)
		}
	}
}

// IsEmpty returns whether the tracker is empty.
func (mt *MedianTracker) IsEmpty() bool {
	return mt.minHeap.Len() == 0 && mt.maxHeap.Len() == 0
}

// Median returns the current median value in the tracker. It is fine to mix and
// match calls to Add and Median. However, it is illegal to call Median on an
// empty tracker.
func (mt *MedianTracker) Median() float64 {
	if mt.IsEmpty() {
		fmt.Printf("maxL = %d, minL = %d\n", mt.maxHeap.Len(), mt.minHeap.Len())
		log.Fatal("GetMedian on empty tracker.\n")
	}

	if mt.maxHeap.Len() > mt.minHeap.Len() {
		return mt.maxHeap.Peek()
	} else if mt.minHeap.Len() > mt.maxHeap.Len() {
		return mt.minHeap.Peek()
	}

	return 0.5 * (mt.minHeap.Peek() + mt.maxHeap.Peek())
}

// Add a value to the max heap, keeping the heaps balanced.
func (mt *MedianTracker) addToMaxHeap(v float64) {
	heap.Push(&mt.maxHeap, v)
	if mt.maxHeap.Len() > mt.minHeap.Len()+1 {
		heap.Push(&mt.minHeap, heap.Pop(&mt.maxHeap))
	}
}

// Add a value to the min heap, keeping the heaps balanced.
func (mt *MedianTracker) addToMinHeap(v float64) {
	heap.Push(&mt.minHeap, v)
	if mt.minHeap.Len() > mt.maxHeap.Len()+1 {
		heap.Push(&mt.maxHeap, heap.Pop(&mt.minHeap))
	}
}
