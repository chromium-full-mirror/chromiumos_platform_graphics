// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package profile

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

// Together these regex are used to filter out c-style comments from json, with
// some limitations. E.g. block comments are no expected on the same lines as json
// code.
var (
	// Regex to match whole-line c-style comment just like this line.
	regexWholeLineComment = regexp.MustCompile(`^\s*//.*`)
	// Regex to match double-backslash comment at end of line. Watch for strings!
	regexCommentAtEndOfLine = regexp.MustCompile(`^([^"]*(?:".*")*[^"/]*)//+.*$`)
	// Regex to match start of block comment. Limitation: no code expected on same line.
	regexStartOfBlockComment = regexp.MustCompile(`^\s*/\*.*`)
	// Regex to match end of block comment. Limitation: no code expected on same line.
	regexEndOfBlockComment = regexp.MustCompile(`.*\*/\s*$`)
)

// ConfigPropertyHandler is an interfaces for handlers associated with top-level
// properties in the config file. Handlers are associated with top-level
// properties by calling function AddHandler on JSONConfigParser.
type ConfigPropertyHandler interface {
	ParseJSONData(propName, jsonData string) error
}

// JSONConfigParser is a helper class for parsing JSON configuration files. By
// itself, JSONConfigParser scans top-level properties JSON files and recursively
// processes config files listed in property with name "include". To be useful
// handlers must be associated with top-level properties other than "include".
type JSONConfigParser struct {
	handlers map[string]ConfigPropertyHandler
	jsonData map[string]interface{}

	// Each time a file is opened, its base path (dir) is pushed onto this stack.
	// When a file to be opened has a relative path, it is considered to be relative
	// to the base path at the top of the stack. That effectively makes an included
	// file relative to the path of the file that contains the include.
	basePathStack []string
}

// CreateJSONConfigParser creates and returns a JSONConfigParser instance.
func CreateJSONConfigParser() *JSONConfigParser {
	return &JSONConfigParser{
		handlers:      make(map[string]ConfigPropertyHandler),
		basePathStack: make([]string, 0, 11),
	}
}

// SetBasePath set the base paths for all subsequent includes. This is optional,
// but when used it must be done before opening or including any file.
func (jc *JSONConfigParser) SetBasePath(basePath string) {
	if len(jc.basePathStack) == 0 {
		jc.basePathStack = append(jc.basePathStack, basePath)
	} else {
		log.Fatal("Error: trying to set include base path after opening files.\n")
	}
}

// GetBasePath returns the current base path.
func (jc *JSONConfigParser) GetBasePath() string {
	return jc.peekBasePath()
}

// AddHandler adds a handler for property with name <fieldName> to the
// JSONConfigParser instance. The handler will be invoked each time a top-level
// property with that name is found in the config file or in the included files.
func (jc *JSONConfigParser) AddHandler(
	fieldName string, handler ConfigPropertyHandler) error {

	if fieldName == "include" {
		return fmt.Errorf("handlers may not be associated with property \"include\"")
	}

	jc.handlers[fieldName] = handler
	return nil
}

// OpenJSONConfigFile open json file with path <jsonFile> and ensures it is ready
// for processing. If the file doesn't look like a file that can be processed
// with this JSONConfigParser instance, an error is returned.
func (jc *JSONConfigParser) OpenJSONConfigFile(jsonFile string) error {
	file, err := jc.openFileAndPushBasePath(jsonFile)
	if err != nil {
		return err
	}
	defer file.Close()

	// Note: We leave the base path pushed on the stack by openFileAndPushBasePath.
	// It effectively becomes the root base path for all subsequent includes and
	// file opens.

	jsonReader := filterCommentsFromStream(file)
	return jc.OpenJSONFromReader(jsonReader)
}

// OpenJSONFromReader opens the parser with JSON data from the given reader and
// ensures it is ready for processing. If the JSON data doesn't look like it
// can be processed with this JSONConfigParser instance, an error is returned.
func (jc *JSONConfigParser) OpenJSONFromReader(jsonReader io.Reader) error {
	var decode = json.NewDecoder(jsonReader)
	if err := decode.Decode(&jc.jsonData); err != nil {
		return err
	}

	if !jc.canProcessData(jc.jsonData) {
		return fmt.Errorf("JSON has no usable configuration data")
	}

	return nil
}

// Process processes a json config file that was successfully opened with
// OpenJSONConfigFile.
func (jc *JSONConfigParser) Process() error {
	return jc.processJSONData(jc.jsonData)
}

// Returns whether a json data map (a map of property names to values) is one
// that can be processed by JSONConfig. That is so when the json data has either
// an "include" property or at least one property that can be processed by one
// of the available handlers.
func (jc *JSONConfigParser) canProcessData(jsonData map[string]interface{}) bool {
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
func (jc *JSONConfigParser) processJSONData(data map[string]interface{}) error {
	// Process the include files first.
	if includes, ok := data["include"]; ok {
		if err := jc.includeFiles(includes); err != nil {
			return err
		}
	}

	// Process property values that have an associated handler.
	for propertyName := range data {
		if handler, ok := jc.handlers[propertyName]; ok {
			if err := jc.invokeHandler(handler, propertyName, data[propertyName]); err != nil {
				return err
			}
		}
	}

	return nil
}

// Process included files up to the maximum include depth.
func (jc *JSONConfigParser) includeFiles(files interface{}) error {
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

	// Process the included files in the order found. Included files may themselves
	// include other files. However, the capacity of the base-path stack is fixed.
	// That ensures that circular references will not cause infinite recursion.
	for _, oneFile := range fileList {
		fileHandle, err := jc.openFileAndPushBasePath(oneFile)
		if err != nil {
			return err
		}

		err = jc.parseFile(fileHandle)
		fileHandle.Close()
		jc.popBasePath()
		if err != nil {
			return err
		}
	}

	return nil
}

// Process a json file.
func (jc *JSONConfigParser) parseFile(file *os.File) error {
	var decode = json.NewDecoder(file)
	var data map[string]interface{}
	if err := decode.Decode(&data); err != nil {
		return err
	}

	return jc.processJSONData(data)
}

// Look for a handler associated with a top-level property and invoke it if found.
func (jc *JSONConfigParser) invokeHandler(handler ConfigPropertyHandler, propName string, data interface{}) error {
	// The handler expect the json data as a string. So we must map the interface{} value
	// back to a string. We can do that with the json encoder.
	var buffer = new(bytes.Buffer)
	var encode = json.NewEncoder(buffer)
	if err := encode.Encode(data); err != nil {
		return err
	}

	return handler.ParseJSONData(propName, buffer.String())
}

// Try to open the file for reading and return its os.File handle. If the  file
// path is relative, it is taken to be relative to the current base path. If the
// file is successfully opened, its dir becomes the most current base path.
func (jc *JSONConfigParser) openFileAndPushBasePath(file string) (*os.File, error) {
	if jc.isBasePathStackFull() {
		return nil, fmt.Errorf("too many includes: %d", len(jc.basePathStack))
	}

	absPath, err := jc.getAbsolutePath(file)
	if err != nil {
		return nil, err
	}

	f, err := os.Open(absPath)
	if err != nil {
		return nil, fmt.Errorf("cannot open file <%s>; error=%w", absPath, err)
	}

	jc.pushBasePath(filepath.Dir(absPath))
	return f, nil
}

// Returns whether the base-path stack is empty.
func (jc *JSONConfigParser) isBasePathStackEmpty() bool {
	return len(jc.basePathStack) == 0
}

// Returns whether the base-path stack is full.
func (jc *JSONConfigParser) isBasePathStackFull() bool {
	return len(jc.basePathStack) == cap(jc.basePathStack)
}

// Push path bp onto the base-path stack.
func (jc *JSONConfigParser) pushBasePath(bp string) {
	if len(jc.basePathStack) < cap(jc.basePathStack) {
		jc.basePathStack = append(jc.basePathStack, bp)
	}
}

// Pop and discard the top-most path from the base-path stack.
func (jc *JSONConfigParser) popBasePath() {
	if len(jc.basePathStack) > 0 {
		jc.basePathStack = jc.basePathStack[:len(jc.basePathStack)-1]
	}
}

// Return the top-most path from the base-path stack.
func (jc *JSONConfigParser) peekBasePath() string {
	if len(jc.basePathStack) > 0 {
		return jc.basePathStack[len(jc.basePathStack)-1]
	}
	return ""
}

// Returns the absolute path for given file path aPath using the following
// algorithm:
// - If aPath is already an absolute path, return it as-is.
// - If the base-path stack is empty aPath is joined to the current working directory.
// - Otherwise, aPath is joined to the top-most path on the base-path stack.
func (jc *JSONConfigParser) getAbsolutePath(aPath string) (string, error) {
	if filepath.IsAbs(aPath) {
		return aPath, nil
	} else if jc.isBasePathStackEmpty() {
		absPath, err := filepath.Abs(aPath)
		if err != nil {
			return "", err
		}
		return absPath, nil
	}

	absPath := filepath.Join(jc.peekBasePath(), aPath)
	return filepath.Clean(absPath), nil
}

// Filter out a limited form of c-style comments from inStream. Returns a new
// stream that delivers the new json with comments removed.
func filterCommentsFromStream(inStream io.Reader) io.Reader {
	outJSON := strings.Builder{}
	lines := bufio.NewScanner(inStream)
	inBlockComment := false
	for lines.Scan() {
		line := []byte(lines.Text())
		if inBlockComment {
			// Within a multi-line block comment: we only look for end-of-block comments
			// and all lines are ignored.
			if regexEndOfBlockComment.Match(line) {
				inBlockComment = false
			}
		} else {
			// Whole-line comments like this line are simply dropped.
			if regexWholeLineComment.Match(line) {
				continue
			}

			// Kept together and in this order, the next two if statements take care
			// of block comments that start and end on the same line.
			if regexStartOfBlockComment.Match(line) {
				inBlockComment = true
			}
			if regexEndOfBlockComment.Match(line) {
				inBlockComment = false
				continue
			}

			// Look for double-backslash end-of-line comments and remove them.
			if match := regexCommentAtEndOfLine.FindSubmatch(line); match != nil {
				line = match[1]
			}

			if !inBlockComment {
				outJSON.Write(line)
			}
		}
	}

	return strings.NewReader(outJSON.String())
}
