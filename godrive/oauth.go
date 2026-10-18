package main

import (
	"context"
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"time"

	"golang.org/x/oauth2"
	"golang.org/x/oauth2/google"
	"google.golang.org/api/drive/v3"
	"google.golang.org/api/option"
)

const driveScope = "https://www.googleapis.com/auth/drive"

// googleAuthURL/googleTokenURL/googleDeviceAuthURL are overridable in tests
// so oauth_test.go can point them at an httptest.Server instead of real
// Google endpoints (Plan §8.4's "no real network" testing requirement).
var (
	googleAuthURL       = google.Endpoint.AuthURL
	googleTokenURL      = google.Endpoint.TokenURL
	googleDeviceAuthURL = google.Endpoint.DeviceAuthURL
)

// loginTimeout bounds how long gda_login("browser") blocks waiting for the
// loopback callback; overridable in tests.
var loginTimeout = 5 * time.Minute

type ClientConfig struct {
	ClientID     string `json:"client_id"`
	ClientSecret string `json:"client_secret"`
}

// LoadClientConfig reads ~/.config/google-drive-agent/client.json. A
// missing file is a clear INTERNAL failure pointing at the one-time setup
// (README.md §3), per the module brief.
func LoadClientConfig() (*ClientConfig, error) {
	path, err := clientConfigPath()
	if err != nil {
		return nil, newAppError("INTERNAL", 0, err.Error(), false)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, newAppError("INTERNAL", 0,
			fmt.Sprintf("missing OAuth client config at %s; run the one-time setup in README.md §3 first", path), false)
	}
	var cfg ClientConfig
	if err := json.Unmarshal(data, &cfg); err != nil {
		return nil, newAppError("INTERNAL", 0, fmt.Sprintf("invalid client.json: %v", err), false)
	}
	if cfg.ClientID == "" {
		return nil, newAppError("INTERNAL", 0, "client.json is missing client_id", false)
	}
	return &cfg, nil
}

func oauthConfig(cfg *ClientConfig, redirectURL string) *oauth2.Config {
	return &oauth2.Config{
		ClientID:     cfg.ClientID,
		ClientSecret: cfg.ClientSecret,
		Scopes:       []string{driveScope},
		RedirectURL:  redirectURL,
		Endpoint: oauth2.Endpoint{
			AuthURL:       googleAuthURL,
			TokenURL:      googleTokenURL,
			DeviceAuthURL: googleDeviceAuthURL,
		},
	}
}

// Login implements gda_login (FFI-Contract.md §3.1). mode is validated
// before touching client.json/network so a bad argument always surfaces as
// INVALID_ARGUMENT, even when the one-time OAuth setup hasn't been done yet.
func Login(mode string) error {
	if mode != "" && mode != "browser" && mode != "device" {
		return newAppError("INVALID_ARGUMENT", 0, fmt.Sprintf("unsupported login mode: %q", mode), false)
	}

	cfg, err := LoadClientConfig()
	if err != nil {
		return err
	}

	if mode == "device" {
		return loginDevice(cfg)
	}
	return loginBrowser(cfg)
}

// Logout implements gda_logout (FFI-Contract.md §3.2).
func Logout() error {
	return DeleteToken()
}

func generateState() string {
	buf := make([]byte, 16)
	_, _ = rand.Read(buf)
	return base64.RawURLEncoding.EncodeToString(buf)
}

// loginBrowser runs the PKCE S256 + one-shot loopback flow (Plan §6.4
// steps 2-5).
func loginBrowser(cfg *ClientConfig) error {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return newAppError("INTERNAL", 0, fmt.Sprintf("starting loopback listener: %v", err), false)
	}
	defer listener.Close()

	redirectURL := fmt.Sprintf("http://127.0.0.1:%d/callback", listener.Addr().(*net.TCPAddr).Port)
	oc := oauthConfig(cfg, redirectURL)

	verifier := oauth2.GenerateVerifier()
	state := generateState()

	codeCh := make(chan string, 1)
	errCh := make(chan error, 1)

	mux := http.NewServeMux()
	mux.HandleFunc("/callback", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Query().Get("state") != state {
			errCh <- fmt.Errorf("state mismatch")
			http.Error(w, "state mismatch", http.StatusBadRequest)
			return
		}
		if errParam := r.URL.Query().Get("error"); errParam != "" {
			errCh <- fmt.Errorf("authorization denied: %s", errParam)
			fmt.Fprintln(w, "Authorization failed, you can close this tab.")
			return
		}
		code := r.URL.Query().Get("code")
		fmt.Fprintln(w, "Login complete, you can close this tab.")
		codeCh <- code
	})
	server := &http.Server{Handler: mux}
	go server.Serve(listener)
	defer server.Close()

	authURL := oc.AuthCodeURL(state, oauth2.AccessTypeOffline, oauth2.S256ChallengeOption(verifier))

	// Always print the URL first, rather than only on a Start() error.
	// xdg-open commonly EXISTS but still can't open anything (headless
	// WSL/SSH sessions with no DISPLAY/browser registered) -- Start()
	// only reports whether the process launched, not whether it actually
	// opened a browser, so gating this message on its error leaves the
	// terminal silent for minutes on exactly the machines that need it.
	fmt.Fprintln(os.Stderr, "Open this URL in a browser to continue login:")
	fmt.Fprintln(os.Stderr, authURL)
	_ = exec.Command("xdg-open", authURL).Start()

	var code string
	select {
	case code = <-codeCh:
	case loginErr := <-errCh:
		return newAppError("UNAUTHENTICATED", 0, fmt.Sprintf("login failed: %v", loginErr), false)
	case <-time.After(loginTimeout):
		return newAppError("INTERNAL", 0, "login timed out waiting for the browser callback", false)
	}

	tok, err := oc.Exchange(context.Background(), code, oauth2.VerifierOption(verifier))
	if err != nil {
		return newAppError("UNAUTHENTICATED", 0, fmt.Sprintf("exchanging authorization code: %v", err), false)
	}
	return SaveToken(tok)
}

// loginDevice runs the Google device-code flow (Plan §6.4 step 6), printing
// the user_code/verification URL directly to the shared terminal rather
// than through the FFI return value (FFI-Contract.md §3.1).
func loginDevice(cfg *ClientConfig) error {
	oc := oauthConfig(cfg, "")
	ctx := context.Background()

	da, err := oc.DeviceAuth(ctx, oauth2.AccessTypeOffline)
	if err != nil {
		return newAppError("INTERNAL", 0, fmt.Sprintf("starting device authorization: %v", err), false)
	}

	fmt.Println("To finish signing in, visit:", da.VerificationURI)
	fmt.Println("And enter code:", da.UserCode)
	if da.VerificationURIComplete != "" {
		fmt.Println("Or open directly:", da.VerificationURIComplete)
	}

	tok, err := oc.DeviceAccessToken(ctx, da)
	if err != nil {
		return newAppError("INTERNAL", 0, fmt.Sprintf("device authorization did not complete: %v", err), false)
	}
	return SaveToken(tok)
}

// getDriveService builds an authenticated *drive.Service from the stored
// token, auto-refreshing (and persisting the refreshed token) as needed.
func getDriveService(ctx context.Context) (*drive.Service, error) {
	tok, err := LoadToken()
	if err != nil {
		return nil, newAppError("INTERNAL", 0, err.Error(), false)
	}
	if tok == nil {
		return nil, newAppError("UNAUTHENTICATED", 0, "not logged in; run `login` first", false)
	}
	cfg, err := LoadClientConfig()
	if err != nil {
		return nil, err
	}
	oc := oauthConfig(cfg, "")
	ts := &persistingTokenSource{base: oc.TokenSource(ctx, tok)}
	srv, err := drive.NewService(ctx, option.WithTokenSource(ts))
	if err != nil {
		return nil, newAppError("INTERNAL", 0, fmt.Sprintf("creating Drive client: %v", err), false)
	}
	return srv, nil
}
