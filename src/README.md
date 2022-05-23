# ChromeOS graphics utilities

This folder contains independent and cross platform utilities for the ChromeOS graphics team.

[TOC]

## Overview

Golang based apps are built and installed using the graphics-utils-go.ebuild.
For this to work the procject directory structure should be as follows

* graphics/src/go.chromium.org/chromiumos/graphics-utils-go/
  * Project1/
    * cmd/
      * App1/
        * main.go
      * App2/
        * main.go
    * bin/
    * Makefile
  * Project2/
    * cmd/
      * App3/
        * main.go
    * Makefile
