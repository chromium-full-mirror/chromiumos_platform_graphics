// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package profile

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"reflect"

	"go.chromium.org/chromiumos/graphics-utils-go/trace_profiling/cmd/profile/remote"
)

// ConfigFiles contains the paths to the three JSON configuration files that
// are needed for profiling on remote devices. If the tunnel config file is not
// needed, it should be set to the empty string.
type ConfigFiles struct {
	SSHConfigFile     string `json:"sshConfigFile"`
	TunnelConfigFile  string `json:"tunnelConfigFile"`
	ProfileConfigFile string `json:"profileConfigFile"`
}

// Configs encapsulates JSON configurations for SSH, tunneling and profiling.
// All of these configurations are optional and, if specified, they do not need
// to be complete. Ecah value that is specified override the corresponding value
// read from the JSON file configuration.
type Configs struct {
	SSH     *map[string]interface{} `json:"ssh"`
	Tunnel  *map[string]interface{} `json:"tunnel"`
	Profile *map[string]interface{} `json:"profile"`
}

// Bundle is a JSON-compatible struct with two optional fields for JSON
// configuration files and JSON configuration data.
type Bundle struct {
	Files      *ConfigFiles `json:"files"`
	ConfigData *Configs     `json:"configs"`
}

// ReadConfigBundle reads a JSON configuration Bundle from a file, parses it
// and returns the SSHParams, TunnelParams and ProfileParams objects it creates
// from that bundle.
func ReadConfigBundle(bundleFile string) (*remote.SSHParams, *remote.TunnelParams, *ProfileParams, error) {
	file, err := os.Open(bundleFile)
	if err != nil {
		return nil, nil, nil, fmt.Errorf("Error: Cannot open file <%s>; error=%w",
			bundleFile, err)
	}
	defer file.Close()

	bundleData, err := ioutil.ReadAll(file)
	if err != nil {
		return nil, nil, nil, fmt.Errorf("Error: Cannot read file <%s>; error=%w",
			bundleFile, err)
	}

	var bundle Bundle
	err = json.Unmarshal(bundleData, &bundle)
	if err != nil {
		return nil, nil, nil, fmt.Errorf("Error: failed to parse JSON <%s>; error=%w",
			bundleFile, err)
	}

	// Read and parse the individual configuration files
	var sshParams *remote.SSHParams
	var tunnelParams *remote.TunnelParams
	var profileParams *ProfileParams
	if bundle.Files != nil {
		sshParams, tunnelParams, profileParams, err = parseFiles(bundle.Files)
		if err != nil {
			return nil, nil, nil, err
		}
	}

	if bundle.ConfigData != nil {
		sshParams, tunnelParams, profileParams, err = parseConfigData(
			bundle.ConfigData, sshParams, tunnelParams, profileParams)
		if err != nil {
			return nil, nil, nil, err
		}
	}

	return sshParams, tunnelParams, profileParams, nil
}

// Parse the JSON config files and return the corresponding SSHParams,
// TunnelParams and ProfileParams objects. Any of them may be nil if the
// corresponding file is not present or not specified.
func parseFiles(files *ConfigFiles) (ssh *remote.SSHParams, tunnel *remote.TunnelParams, profile *ProfileParams, err error) {
	if files.SSHConfigFile != "" {
		ssh, err = remote.CreateSSHParamsFromJSON(files.SSHConfigFile)
		if err != nil {
			return
		}
	}

	if files.TunnelConfigFile != "" {
		tunnel, err = remote.ReadTunnelParamsFromJSON(files.TunnelConfigFile)
		if err != nil {
			return
		}
	}

	if files.ProfileConfigFile != "" {
		profile, err = CreateProfileParamsFromJSON(files.ProfileConfigFile)
		if err != nil {
			return
		}
	}

	err = nil
	return
}

// Parse the JSON config data and use the properties found in that data to
// override the values already present in the input struct. It's ok for any
// of the input struct to be nil. In that case, a new struct is created and
// returned if data is found in the JSON config.
func parseConfigData(
	configs *Configs,
	sshIn *remote.SSHParams,
	tunnelIn *remote.TunnelParams,
	profileIn *ProfileParams) (
	sshOut *remote.SSHParams,
	tunnelOut *remote.TunnelParams,
	profileOut *ProfileParams,
	err error) {

	if configs.SSH != nil {
		if sshIn == nil {
			sshIn = &remote.SSHParams{}
		}
		// Iterate of the properties found in the JSON data and assign each value
		// to the coresponding field in the struct.
		var p *map[string]interface{} = configs.SSH
		for k, v := range *p {
			err = setStructFieldByName(sshIn, k, v)
			if err != nil {
				return
			}
		}
	}
	sshOut = sshIn

	if configs.Tunnel != nil {
		if tunnelIn == nil {
			tunnelIn = &remote.TunnelParams{}
		}
		// Iterate of the properties found in the JSON data and assign each value
		// to the coresponding field in the struct.
		var p *map[string]interface{} = configs.Tunnel
		for k, v := range *p {
			err = setStructFieldByName(tunnelIn, k, v)
			if err != nil {
				return
			}
		}
	}
	tunnelOut = tunnelIn

	if configs.Profile != nil {
		if profileIn == nil {
			profileIn = &ProfileParams{}
		}
		// Iterate of the properties found in the JSON data and assign each value
		// to the coresponding field in the struct.
		var p *map[string]interface{} = configs.Profile
		for k, v := range *p {
			err = setStructFieldByName(profileIn, k, v)
			if err != nil {
				return
			}
		}
	}
	profileOut = profileIn

	return
}

// Generic obj is one of the three known struct inside a Bundle. Set its field
// identified by the JSON tag <key> to <value>. This function works if the
// field is one of the known nested struct or array.
func setStructFieldByName(obj interface{}, key string, value interface{}) error {
	var structType reflect.Type
	var structElem reflect.Value

	// Use reflection on known struct types to extract the correct type and elem.
	if st, ok := obj.(*ProfileParams); ok {
		structType = reflect.TypeOf(*st)
		structElem = reflect.ValueOf(st).Elem()
	} else if st, ok := obj.(*remote.SSHParams); ok {
		structType = reflect.TypeOf(*st)
		structElem = reflect.ValueOf(st).Elem()
	} else if st, ok := obj.(*remote.TunnelParams); ok {
		structType = reflect.TypeOf(*st)
		structElem = reflect.ValueOf(st).Elem()
	} else {
		// This would be a coding error, likely obj is not one of the three
		// supported struct type.
		panic("Error: unknown struct type")
	}

	// Retrieve the struct field by its json key name.
	var fieldByKey *reflect.StructField
	for i := 0; i < structType.NumField(); i++ {
		field := structType.Field(i)
		if field.Tag.Get("json") == key {
			fieldByKey = &field
		}
	}

	if fieldByKey == nil {
		// Silently ignore unrecognized JSON properties.
		return nil
	}

	// Now assign the new value to that field.
	structField := structElem.FieldByName(fieldByKey.Name)
	switch structField.Kind() {
	case reflect.Array, reflect.Slice:
		// Valid JSON for Bundle only has array of strings. So lets try to convert
		// value to an array of strings and fail if that doesn't work.
		arr, err := toStringArray(value, key)
		if err != nil {
			return err
		}
		structField.Set(reflect.ValueOf(arr))
	case reflect.Struct:
		// The only nested struct is the server SSH params inside TunnelParams.
		// We use that knowledge to greatly simplify the code. We cast obj back to
		// its typed struct (st) and access the field st.server directly.
		if st, ok := obj.(*remote.TunnelParams); ok {
			// Only maps can be assigned from JSON to struct.
			valueAsMap, ok := value.(map[string](interface{}))
			if !ok {
				return fmt.Errorf("Error: JSON property %s is not assignable to field %s",
					key, fieldByKey.Name)
			}
			return copySSHProperties(&valueAsMap, &(st.Server))
		}

		panic(fmt.Sprintf("Unexpected nested struct in %s", structType.Name()))
	default:
		if jsonVal, ok := value.(float64); ok {
			// JSON values are converted to float64, but all values in the supported
			// struct are int. If we find a float64, convert to and store it as int.
			var intVal int = int(jsonVal)
			structField.Set(reflect.ValueOf(intVal))
		} else {
			// All other field type are strings and can be store directly.
			structField.Set(reflect.ValueOf(value))
		}
	}
	return nil
}

// Convert generic values to an array of strings.
func toStringArray(values interface{}, key string) (strArr []string, err error) {
	// Check that values is a slice or array object.
	valueKind := reflect.TypeOf(values).Kind()
	if valueKind != reflect.Slice && valueKind != reflect.Array {
		err = fmt.Errorf("Error: JSON property %s has the wrong type", key)
		return
	}

	// Assign each item in values to a new arry of strings. Fails if values is
	// an array of some other item type.
	inArr := reflect.ValueOf(values)
	for i := 0; i < inArr.Len(); i++ {
		str := inArr.Index(i).Interface()
		if s, ok := str.(string); ok {
			strArr = append(strArr, s)
		} else {
			err = fmt.Errorf("Error: expected an array of string for JSON property %s", key)
			return
		}
	}
	return
}

// Copy the property values found in the map to struct toSSHParams.
func copySSHProperties(
	properties *map[string]interface{}, toSSHParam *remote.SSHParams) error {
	for k, v := range *properties {
		err := setStructFieldByName(toSSHParam, k, v)
		if err != nil {
			return err
		}
	}
	return nil
}
