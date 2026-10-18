package main

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/url"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"golang.org/x/oauth2"
)

// --- PKCE challenge generation (browser mode) -----------------------------

func TestPKCEChallenge_AuthCodeURLCarriesS256Challenge(t *testing.T) {
	verifier := oauth2.GenerateVerifier()
	require.NotEmpty(t, verifier)

	cfg := &ClientConfig{ClientID: "test-client", ClientSecret: "test-secret"}
	oc := oauthConfig(cfg, "http://127.0.0.1:0/callback")

	authURL := oc.AuthCodeURL("state-123", oauth2.AccessTypeOffline, oauth2.S256ChallengeOption(verifier))

	parsed, err := url.Parse(authURL)
	require.NoError(t, err)
	q := parsed.Query()

	assert.Equal(t, "S256", q.Get("code_challenge_method"))
	assert.Equal(t, oauth2.S256ChallengeFromVerifier(verifier), q.Get("code_challenge"))
	assert.Equal(t, "state-123", q.Get("state"))
}

func TestPKCEVerifier_SentOnTokenExchange(t *testing.T) {
	var gotVerifier string
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		require.NoError(t, r.ParseForm())
		gotVerifier = r.Form.Get("code_verifier")
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"access_token":  "fake-access-token",
			"refresh_token": "fake-refresh-token",
			"expires_in":    3600,
			"token_type":    "Bearer",
		})
	}))
	defer server.Close()

	cfg := &ClientConfig{ClientID: "test-client", ClientSecret: "test-secret"}
	oc := oauthConfig(cfg, "http://127.0.0.1:0/callback")
	oc.Endpoint.TokenURL = server.URL

	verifier := oauth2.GenerateVerifier()
	tok, err := oc.Exchange(context.Background(), "fake-auth-code", oauth2.VerifierOption(verifier))
	require.NoError(t, err)

	assert.Equal(t, "fake-access-token", tok.AccessToken)
	assert.Equal(t, verifier, gotVerifier)
}

// --- device-code poll state machine ---------------------------------------

func TestLoginDevice_PollsUntilAuthorized(t *testing.T) {
	withTempConfigDir(t)

	pollCount := 0
	mux := http.NewServeMux()
	mux.HandleFunc("/device/code", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"device_code":      "DEVICE123",
			"user_code":        "ABCD-EFGH",
			"verification_uri": "https://example.invalid/device",
			"expires_in":       600,
			"interval":         1, // keep the test fast; RFC8628 min is honored by the client
		})
	})
	mux.HandleFunc("/token", func(w http.ResponseWriter, r *http.Request) {
		pollCount++
		w.Header().Set("Content-Type", "application/json")
		if pollCount < 2 {
			w.WriteHeader(http.StatusBadRequest)
			_ = json.NewEncoder(w).Encode(map[string]any{"error": "authorization_pending"})
			return
		}
		_ = json.NewEncoder(w).Encode(map[string]any{
			"access_token":  "device-access-token",
			"refresh_token": "device-refresh-token",
			"expires_in":    3600,
			"token_type":    "Bearer",
		})
	})
	server := httptest.NewServer(mux)
	defer server.Close()

	oldDevice, oldToken := googleDeviceAuthURL, googleTokenURL
	googleDeviceAuthURL = server.URL + "/device/code"
	googleTokenURL = server.URL + "/token"
	t.Cleanup(func() { googleDeviceAuthURL, googleTokenURL = oldDevice, oldToken })

	err := loginDevice(&ClientConfig{ClientID: "test-client"})
	require.NoError(t, err)
	assert.GreaterOrEqual(t, pollCount, 2, "expected at least one authorization_pending before success")

	saved, err := LoadToken()
	require.NoError(t, err)
	require.NotNil(t, saved)
	assert.Equal(t, "device-access-token", saved.AccessToken)
}

func TestLoginDevice_AccessDeniedIsAFailure(t *testing.T) {
	withTempConfigDir(t)

	mux := http.NewServeMux()
	mux.HandleFunc("/device/code", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{
			"device_code":      "DEVICE123",
			"user_code":        "ABCD-EFGH",
			"verification_uri": "https://example.invalid/device",
			"expires_in":       600,
			"interval":         1,
		})
	})
	mux.HandleFunc("/token", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusBadRequest)
		_ = json.NewEncoder(w).Encode(map[string]any{"error": "access_denied"})
	})
	server := httptest.NewServer(mux)
	defer server.Close()

	oldDevice, oldToken := googleDeviceAuthURL, googleTokenURL
	googleDeviceAuthURL = server.URL + "/device/code"
	googleTokenURL = server.URL + "/token"
	t.Cleanup(func() { googleDeviceAuthURL, googleTokenURL = oldDevice, oldToken })

	err := loginDevice(&ClientConfig{ClientID: "test-client"})
	require.Error(t, err)
	assert.Equal(t, "INTERNAL", mapError(err).Code)

	saved, loadErr := LoadToken()
	require.NoError(t, loadErr)
	assert.Nil(t, saved)
}

// --- client.json loading ---------------------------------------------------

func TestLoadClientConfig_MissingFileIsClearFailure(t *testing.T) {
	withTempConfigDir(t)

	_, err := LoadClientConfig()
	require.Error(t, err)
	appErr := mapError(err)
	assert.Equal(t, "INTERNAL", appErr.Code)
	assert.Contains(t, appErr.Message, "README.md")
}
