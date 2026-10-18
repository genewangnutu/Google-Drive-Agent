package main

import (
	"encoding/json"
	"errors"
	"fmt"

	"google.golang.org/api/googleapi"
)

// AppError is the internal representation of the FFI-Contract.md §1 error
// envelope. Every //export function must end up producing either success
// data or one of these, never a raw Go error or a panic.
type AppError struct {
	Code       string
	HTTPStatus int
	Message    string
	Retryable  bool
}

func (e *AppError) Error() string { return e.Message }

func newAppError(code string, httpStatus int, message string, retryable bool) *AppError {
	return &AppError{Code: code, HTTPStatus: httpStatus, Message: message, Retryable: retryable}
}

// mapError normalizes any error returned from drive_client.go/oauth.go into
// an *AppError, mapping googleapi.Error by HTTP status/reason per Plan §6.5.
func mapError(err error) *AppError {
	if err == nil {
		return nil
	}
	var appErr *AppError
	if errors.As(err, &appErr) {
		return appErr
	}
	var gerr *googleapi.Error
	if errors.As(err, &gerr) {
		return mapGoogleAPIError(gerr)
	}
	return newAppError("INTERNAL", 0, err.Error(), false)
}

func mapGoogleAPIError(gerr *googleapi.Error) *AppError {
	status := gerr.Code
	retryable := status == 429 || status >= 500
	code := "INTERNAL"

	switch {
	case status == 401:
		code = "UNAUTHENTICATED"
	case status == 403:
		code = "PERMISSION_DENIED"
		for _, item := range gerr.Errors {
			if item.Reason == "rateLimitExceeded" || item.Reason == "userRateLimitExceeded" {
				code = "RATE_LIMITED"
				retryable = true
			}
		}
	case status == 404:
		code = "NOT_FOUND"
	case status == 429:
		code = "RATE_LIMITED"
	case status == 400:
		code = "INVALID_ARGUMENT"
	case status >= 500:
		code = "INTERNAL"
	}

	msg := gerr.Message
	if msg == "" {
		msg = gerr.Error()
	}
	return newAppError(code, status, msg, retryable)
}

// envelope is the JSON shape returned across the FFI boundary, see
// FFI-Contract.md §1.
type envelope struct {
	OK    bool            `json:"ok"`
	Data  json.RawMessage `json:"data,omitempty"`
	Error *errorBody      `json:"error,omitempty"`
}

type errorBody struct {
	Code       string `json:"code"`
	HTTPStatus int    `json:"http_status"`
	Message    string `json:"message"`
	Retryable  bool   `json:"retryable"`
}

// successEnvelope marshals data into the {"ok":true,"data":...} shape. If
// marshaling itself fails (should not happen for our own well-typed
// structs), it degrades to a failure envelope rather than panicking.
func successEnvelope(data any) string {
	raw, err := json.Marshal(data)
	if err != nil {
		return failureEnvelope(newAppError("INTERNAL", 0, fmt.Sprintf("failed to marshal response: %v", err), false))
	}
	env := envelope{OK: true, Data: raw}
	out, err := json.Marshal(env)
	if err != nil {
		return failureEnvelope(newAppError("INTERNAL", 0, fmt.Sprintf("failed to marshal envelope: %v", err), false))
	}
	return string(out)
}

// failureEnvelope marshals an *AppError into the {"ok":false,"error":...}
// shape. This never fails: on marshal error it falls back to a hand-built
// JSON literal so the FFI boundary always receives valid JSON.
func failureEnvelope(appErr *AppError) string {
	env := envelope{OK: false, Error: &errorBody{
		Code:       appErr.Code,
		HTTPStatus: appErr.HTTPStatus,
		Message:    appErr.Message,
		Retryable:  appErr.Retryable,
	}}
	out, err := json.Marshal(env)
	if err != nil {
		return `{"ok":false,"error":{"code":"INTERNAL","http_status":0,"message":"failed to marshal error envelope","retryable":false}}`
	}
	return string(out)
}

// errorEnvelope is a convenience for turning any error into the failure
// JSON string in one call.
func errorEnvelope(err error) string {
	return failureEnvelope(mapError(err))
}
