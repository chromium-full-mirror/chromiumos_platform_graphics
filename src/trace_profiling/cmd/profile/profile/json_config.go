// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package profile

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
)

// ConfigPropertyHandler is an interfaces for handlers associated with top-level
// properties in the config file. Handlers are associated with top-level
// properties by calling function AddHandler on JSONConfig.
type ConfigPropertyHandler interface {
	parseJSONData(jsonData string) error
}

// JSONConfig is a helper class for parsing JSON configuration files. By iteself,
// JSONConfig scan top-level properties JSON files and recursively processes
// config files listed in top-level property with name "include". It becomes more
// useful when handlers are associated with top-level properties other than
// "include".
type JSONConfig struct {
	handlers     map[string]ConfigPropertyHandler
	jsonData     map[string]interface{}
	includeDepth int
}

// CreateJSONConfig creates and returns a JSONConfig instance.
func CreateJSONConfig() *JSONConfig {
	var jc = JSONConfig{}
	jc.handlers = make(map[string]ConfigPropertyHandler)
	return &jc
}

// AddHandler adds a handler for property with name <fieldName> to the JSONConfig
// object. The handler will be invoked each time a top-level property with that
// name is found in the config file or in the included files.
func (jc *JSONConfig) AddHandler(fieldName string, handler ConfigPropertyHandler) error {
	if fieldName == "include" {
		return fmt.Errorf("invalid field name for custom handler: %s", fieldName)
	}

	jc.handlers[fieldName] = handler
	return nil
}

// OpenJSONConfigFile open json file with path <jsonFile> and ensures it is ready
// for processing. If the file doesn't look like a file that can be processed
// with this JSONConfig instance, an error is returned.
func (jc *JSONConfig) OpenJSONConfigFile(jsonFile string) error {
	file, err := os.Open(jsonFile)
	if err != nil {
		return fmt.Errorf("cannot open config file <%s>; error=%w", jsonFile, err)
	}
	defer file.Close()

	var decode = json.NewDecoder(file)
	if err := decode.Decode(&jc.jsonData); err != nil {
		return err
	}

	if !jc.canProcessData(jc.jsonData) {
		return fmt.Errorf("unable to process JSON data in %s", jsonFile)
	}

	return nil
}

// Process processes a json config file that was successfully opened with
// OpenJSONConfigFile.
func (jc *JSONConfig) Process() error {
	return jc.processJSONData(jc.jsonData)
}

// Returns whether a json data map (a map of property names to values) is one
// that can be processed by JSNConfig. That is so when the json data has either
// an "include" property or at least one property that can be processed by one
// of the available handlers.
func (jc *JSONConfig) canProcessData(jsonData map[string]interface{}) bool {
	if jsonData != nil {
		if _, ok := jsonData["include"]; ok {
			return true
		}

		for propertyName := range jsonData {
			if _, ok := jc.handlers[propertyName]; ok {
				return ok
			}
		}
	}

	return false
}

// Process config json data that is represented as a map of property names to values.
func (jc *JSONConfig) processJSONData(data map[string]interface{}) error {
	// Process the include files first.
	if includes, ok := data["include"]; ok {
		if err := jc.includeFiles(includes); err != nil {
			return err
		}
	}

	// Process property values that have an associated handler.
	for propertyName := range data {
		if handler, ok := jc.handlers[propertyName]; ok {
			if err := jc.invokeHandler(handler, data[propertyName]); err != nil {
				return err
			}
		}
	}

	return nil
}

// Process included files up to the maximum include depth.
func (jc *JSONConfig) includeFiles(files interface{}) error {
	// The include property value may be either a single file (a string) or
	// a list of files (array of strings). Map either into a file list.
	fileList := make([]string, 0, 16)
	if f, ok := files.(string); ok {
		fileList = append(fileList, f)
	} else if list, ok := files.([]interface{}); ok {
		for _, incFile := range list {
			if f, ok := incFile.(string); ok {
				fileList = append(fileList, f)
			}
		}
	} else {
		return fmt.Errorf("invalid value type for include: %v", files)
	}

	// Process the included files in the order found. We limit how deep we can
	// recursively include files as a way of avoiding infinite include loops.
	for _, oneFile := range fileList {
		file, err := os.Open(oneFile)
		if err != nil {
			return fmt.Errorf("cannot include file <%s>; error=%w", oneFile, err)
		}
		defer file.Close()

		jc.includeDepth++
		if jc.includeDepth == 10 {
			return fmt.Errorf("too many includes: %d", jc.includeDepth)
		}

		if err := jc.parseFile(file); err != nil {
			return err
		}
		jc.includeDepth--
	}

	return nil
}

// Process a json file.
func (jc *JSONConfig) parseFile(file *os.File) error {
	var decode = json.NewDecoder(file)
	var data map[string]interface{}
	if err := decode.Decode(&data); err != nil {
		return err
	}

	return jc.processJSONData(data)
}

// Look for a handler associated with a top-level property and invoke it if found.
func (jc *JSONConfig) invokeHandler(handler ConfigPropertyHandler, data interface{}) error {
	// The handler expect the json data as a string. So we must map the interface{} value
	// back to a string. We can do that with the json encoder.
	var buffer = new(bytes.Buffer)
	var encode = json.NewEncoder(buffer)
	if err := encode.Encode(data); err != nil {
		return err
	}

	return handler.parseJSONData(buffer.String())
}
