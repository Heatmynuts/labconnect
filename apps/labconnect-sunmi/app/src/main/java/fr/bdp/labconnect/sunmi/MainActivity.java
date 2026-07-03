package fr.bdp.labconnect.sunmi;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;

public class MainActivity extends Activity {
    private static final String HUB_URL = "http://192.168.4.1/";

    private WebView webView;
    private LinearLayout errorPanel;
    private TextView errorTitle;
    private TextView errorBody;
    private TextView statusText;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                        | WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON
                        | WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED
                        | WindowManager.LayoutParams.FLAG_DISMISS_KEYGUARD);
        buildUi();
        configureWebView();
        loadHub();
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(247, 248, 250));

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(14), dp(8), dp(10), dp(8));
        bar.setBackgroundColor(Color.rgb(247, 248, 250));

        TextView title = new TextView(this);
        title.setText("LabConnect Hub");
        title.setTextColor(Color.rgb(17, 19, 24));
        title.setTextSize(18);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        bar.addView(title, new LinearLayout.LayoutParams(0, dp(44), 1));

        statusText = new TextView(this);
        statusText.setText("Chargement");
        statusText.setTextColor(Color.rgb(105, 112, 125));
        statusText.setTextSize(13);
        statusText.setGravity(Gravity.CENTER_VERTICAL | Gravity.RIGHT);
        bar.addView(statusText, new LinearLayout.LayoutParams(dp(96), dp(44)));

        Button homeButton = makeBarButton("Hub");
        homeButton.setOnClickListener(v -> loadHub());
        bar.addView(homeButton, new LinearLayout.LayoutParams(dp(64), dp(38)));

        Button reloadButton = makeBarButton("↻");
        reloadButton.setTextSize(22);
        reloadButton.setOnClickListener(v -> webView.reload());
        LinearLayout.LayoutParams reloadParams = new LinearLayout.LayoutParams(dp(46), dp(38));
        reloadParams.leftMargin = dp(8);
        bar.addView(reloadButton, reloadParams);

        FrameLayout content = new FrameLayout(this);
        webView = new WebView(this);
        content.addView(webView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        errorPanel = buildErrorPanel();
        errorPanel.setVisibility(View.GONE);
        content.addView(errorPanel, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        root.addView(bar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                dp(60)));
        root.addView(content, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                0,
                1));
        setContentView(root);
    }

    private Button makeBarButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextColor(Color.rgb(10, 132, 255));
        button.setTextSize(14);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setAllCaps(false);
        button.setBackgroundColor(Color.TRANSPARENT);
        button.setPadding(0, 0, 0, 0);
        return button;
    }

    private LinearLayout buildErrorPanel() {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(Gravity.CENTER);
        panel.setPadding(dp(28), dp(28), dp(28), dp(28));
        panel.setBackgroundColor(Color.rgb(247, 248, 250));

        errorTitle = new TextView(this);
        errorTitle.setText("Connexion au Hub");
        errorTitle.setTextColor(Color.rgb(17, 19, 24));
        errorTitle.setTextSize(22);
        errorTitle.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        errorTitle.setGravity(Gravity.CENTER);
        panel.addView(errorTitle);

        errorBody = new TextView(this);
        errorBody.setText("Verification du Knob sur 192.168.4.1...");
        errorBody.setTextColor(Color.rgb(105, 112, 125));
        errorBody.setTextSize(15);
        errorBody.setGravity(Gravity.CENTER);
        errorBody.setPadding(0, dp(10), 0, dp(18));
        panel.addView(errorBody);

        Button retry = makePrimaryButton("Reessayer");
        retry.setOnClickListener(v -> loadHub());
        panel.addView(retry, new LinearLayout.LayoutParams(dp(180), dp(48)));
        return panel;
    }

    private Button makePrimaryButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextColor(Color.WHITE);
        button.setTextSize(16);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setAllCaps(false);
        button.setBackgroundColor(Color.rgb(10, 132, 255));
        return button;
    }

    @SuppressLint("SetJavaScriptEnabled")
    private void configureWebView() {
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setDatabaseEnabled(true);
        settings.setLoadWithOverviewMode(true);
        settings.setUseWideViewPort(true);
        settings.setCacheMode(WebSettings.LOAD_NO_CACHE);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);

        webView.setBackgroundColor(Color.rgb(247, 248, 250));
        webView.setLayerType(View.LAYER_TYPE_SOFTWARE, null);
        webView.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageStarted(WebView view, String url, android.graphics.Bitmap favicon) {
                statusText.setText("Chargement");
                errorPanel.setVisibility(View.GONE);
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                view.postDelayed(() -> view.evaluateJavascript(
                        "(document.body && document.body.innerText ? document.body.innerText.trim().length : 0).toString()",
                        value -> {
                            int length = 0;
                            try {
                                length = Integer.parseInt(value.replace("\"", ""));
                            } catch (Exception ignored) {
                            }
                            if (length > 0) {
                                statusText.setText("Connecte");
                                errorPanel.setVisibility(View.GONE);
                            } else {
                                statusText.setText("Page vide");
                                showMessage("Interface vide", "Le Knob repond, mais la page ne s'affiche pas. Touchez Reessayer.");
                            }
                        }), 700);
            }

            @Override
            public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                if (request.isForMainFrame()) showError();
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                view.loadUrl(request.getUrl().toString());
                return true;
            }
        });
    }

    private void loadHub() {
        statusText.setText(isWifiConnected() ? "Verification" : "Wi-Fi ?");
        showMessage("Connexion au Hub", "Verification du Knob sur 192.168.4.1...");
        new Thread(() -> {
            boolean ok = isHubReachable();
            runOnUiThread(() -> {
                if (ok) {
                    statusText.setText("Chargement");
                    errorPanel.setVisibility(View.GONE);
                    webView.loadUrl(HUB_URL + "?app=1&t=" + System.currentTimeMillis());
                } else {
                    showError();
                }
            });
        }).start();
    }

    private void showError() {
        statusText.setText("Hors ligne");
        showMessage("Hub introuvable", "Connectez le Sunmi au reseau Wi-Fi du Knob, puis touchez Reessayer.");
    }

    private void showMessage(String title, String body) {
        errorTitle.setText(title);
        errorBody.setText(body);
        errorPanel.setVisibility(View.VISIBLE);
    }

    private boolean isHubReachable() {
        HttpURLConnection connection = null;
        try {
            URL url = new URL(HUB_URL + "api/nodes");
            connection = (HttpURLConnection) url.openConnection();
            connection.setConnectTimeout(1800);
            connection.setReadTimeout(1800);
            connection.setUseCaches(false);
            connection.setRequestMethod("GET");
            int code = connection.getResponseCode();
            return code >= 200 && code < 500;
        } catch (IOException ignored) {
            return false;
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    private boolean isWifiConnected() {
        ConnectivityManager cm = (ConnectivityManager) getSystemService(CONNECTIVITY_SERVICE);
        if (cm == null) return false;
        Network network = cm.getActiveNetwork();
        if (network == null) return false;
        NetworkCapabilities caps = cm.getNetworkCapabilities(network);
        return caps != null && caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    public void onBackPressed() {
        if (webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }
}
