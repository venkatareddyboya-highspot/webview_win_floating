#include "my_webview.h"

#include <functional>
#include <iostream>
#include <map>
#include <regex>

#include <windows.h>
#include <WebView2.h>

#include <wrl.h>
#include <wil/com.h>

using namespace Microsoft::WRL;

std::string utf8_encode(const std::wstring& wstr)
{
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::string Utf8FromUtf16(LPWSTR wstr) {
    DWORD dBufSize = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, FALSE);
    char* dBuf = new char[dBufSize];
    int nRet = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, dBuf, dBufSize, NULL, FALSE);
    if (nRet <= 0) return "";
    std::string result = std::string(dBuf);
    delete[]dBuf;
    return result;
}

// --------------------------------------------------------------------------

class MyWebViewImpl : public MyWebView
{
public:
    MyWebViewImpl(HWND hWnd,
        std::function<void(HRESULT, MyWebView*)> onCreated,
        std::function<void(std::string url, bool isNewWindow, bool isUserInitiated)> onPageStarted,
        std::function<void(std::string, int errCode)> onPageFinished,
        std::function<void(std::string)> onPageTitleChanged,
        std::function<void(std::string)> onWebMessageReceived,
        std::function<void(bool)> onMoveFocusRequest,
        std::function<void(bool)> onFullScreenChanged,
        std::function<void()> onHistoryChanged,
        OnAskPermissionFunc onAskPermission,
        PCWSTR pwUserDataFolder);

    virtual ~MyWebViewImpl() override;

	void setHasNavigationDecision(bool hasNavigationDecision);

    HRESULT loadUrl(PCWSTR url);
    HRESULT loadHtmlString(PCWSTR html);
    HRESULT runJavascript(PCWSTR javaScriptString, bool ignoreResult, std::function<void(std::string)> callback);

    HRESULT addScriptChannelByName(LPCWSTR channelName);
    void removeScriptChannelByName(LPCWSTR channelName);

    void enableJavascript(bool bEnable);

    void enableStatusBar(bool bEnable);

    void enableIsZoomControl(bool bEnable);

    HRESULT setUserAgent(LPCWSTR userAgent);

    HRESULT updateBounds(RECT& bounds);
    HRESULT getBounds(RECT& bounds);
    HRESULT setVisible(bool isVisible);
    HRESULT setBackgroundColor(int32_t argb);
    HRESULT requestFocus(bool isNext);

    bool canGoBack();
    bool canGoForward();
    void goBack();
    void goForward();
    void reload();
    void cancelNavigate();

    HRESULT clearCache();
    HRESULT clearCookies();
    HRESULT setCookie(LPCWSTR name, LPCWSTR value, LPCWSTR domain, LPCWSTR path);
    HRESULT getCookies(LPCWSTR uri, std::function<void(std::string)> callback);

	HRESULT suspend();
	HRESULT resume();

    void askFlutterPermission(wil::com_ptr<ICoreWebView2PermissionRequestedEventArgs> args, OnAskPermissionFunc onAskPermission);
    void MyWebViewImpl::grantPermission(int deferralId, BOOL isGranted);

    void openDevTools() override;

private:
    template<class T> wil::com_ptr<T> getProfile();
    std::wstring nowLoadingUrl;
    bool m_isNowGoBackForward = false;
  	bool m_hasNavigationDecision = false;

    std::map<std::wstring, std::wstring> channelMap; // channel name -> id of RemoveScriptToExecuteOnDocumentCreated
    bool m_hasRegisteredChannel = false;

    std::map<int, std::pair< wil::com_ptr<ICoreWebView2PermissionRequestedEventArgs>, wil::com_ptr<ICoreWebView2Deferral> >> permissionArgsMap;
    int lastPermissionDeferralId = 0;

    wil::com_ptr<ICoreWebView2> m_pWebview;
    wil::com_ptr<ICoreWebView2Controller> m_pController;
    wil::com_ptr<ICoreWebView2Settings> m_pSettings;
    RECT m_bounds = { 0,0,0,0 };
};
wil::com_ptr<ICoreWebView2Environment> g_env;

#include <map>
std::map<UINT64, std::string> g_navigationMap;

// --------------------------------------------------------------------------

MyWebView* MyWebView::Create(HWND hWnd,
    std::function<void(HRESULT, MyWebView*)> callback,
    std::function<void(std::string url, bool isNewWindow, bool isUserInitiated)> onPageStarted,
    std::function<void(std::string, int errCode)> onPageFinished,
    std::function<void(std::string)> onPageTitleChanged,
    std::function<void(std::string)> onWebMessageReceived,
    std::function<void(bool)> onMoveFocusRequest,
    std::function<void(bool)> onFullScreenChanged,
    std::function<void()> onHistoryChanged,
    OnAskPermissionFunc onAskPermission,
    PCWSTR pwUserDataFolder)
{
    return new MyWebViewImpl(hWnd, callback, onPageStarted, onPageFinished, onPageTitleChanged, onWebMessageReceived, onMoveFocusRequest, onFullScreenChanged, onHistoryChanged, onAskPermission, pwUserDataFolder);
}

HRESULT InitWebViewRuntime(PCWSTR pwUserDataFolder, std::function<void(HRESULT)> callback = nullptr)
{
    if (g_env != NULL) {
        if (callback != nullptr) callback(S_OK);
        return S_OK;
    }

    return CreateCoreWebView2EnvironmentWithOptions(nullptr, pwUserDataFolder, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [callback](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                g_env = env;
                if (callback != nullptr) callback(result);
                return result;
            }).Get());
}

HRESULT ReleaseWebViewRuntime()
{
    return S_OK;
}

MyWebViewImpl::MyWebViewImpl(HWND hWnd,
    std::function<void(HRESULT, MyWebView*)> onCreated,
    std::function<void(std::string url, bool isNewWindow, bool isUserInitiated)> onPageStarted,
    std::function<void(std::string, int errCode)> onPageFinished,
    std::function<void(std::string)> onPageTitleChanged,
    std::function<void(std::string)> onWebMessageReceived,
    std::function<void(bool)> onMoveFocusRequest,
    std::function<void(bool)> onFullScreenChanged,
    std::function<void()> onHistoryChanged,
    OnAskPermissionFunc onAskPermission,
    PCWSTR pwUserDataFolder = NULL)
{
    InitWebViewRuntime(pwUserDataFolder, [=](HRESULT hr) -> void {
        if (hr != S_OK) {
            onCreated(hr, NULL);
            return;
        }

        g_env->CreateCoreWebView2Controller(hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [=](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                if (hr != S_OK) {
                    onCreated(hr, NULL);
                    return hr;
                }

                hr = controller->get_CoreWebView2(&m_pWebview);
                hr = m_pWebview->get_Settings(&m_pSettings);
                m_pController = controller;

                m_pSettings->put_AreDefaultContextMenusEnabled(FALSE);
#ifndef _DEBUG
                m_pSettings->put_AreDevToolsEnabled(FALSE);
#endif

                m_pWebview->add_NavigationStarting(
                    Callback<ICoreWebView2NavigationStartingEventHandler>(
                        [=](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                            wil::unique_cotaskmem_string url;
                            UINT64 navigationId = 0;
                            BOOL isRedirected = FALSE;
                            BOOL isPostMethod = FALSE;
                            ICoreWebView2HttpRequestHeaders* headers = nullptr;
                            HRESULT hr = args->get_Uri(&url);
                            args->get_NavigationId(&navigationId);
                            args->get_IsRedirected(&isRedirected);

                            args->get_RequestHeaders(&headers);
                            if (headers != nullptr) {
                                // http POST method always set "Content-Type" header,
                                // so if "Content-Type" header exists,
                                // always allow navigation, without asking client code in dart side.
                                // If we skip the POST request below, all the headers will be discard,
                                // so the POST request will be failed, and this makes most of the html login-form failed.
                                headers->Contains(L"Content-Type", &isPostMethod);
                            }

                            if (SUCCEEDED(hr)) {
                                auto utf16Url = std::wstring(url.get());
                                auto utf8Url = utf8_encode(utf16Url);
                                g_navigationMap[navigationId] = utf8Url;
                                //bool isAllowed = checkUrlAllowed(utf8Url); // TODO

                                bool userInitiated = true;
                                if (m_isNowGoBackForward
                                    || isPostMethod == TRUE
                                    || isRedirected == TRUE
                                    || nowLoadingUrl.compare(url.get()) == 0
                                    || utf16Url.rfind(L"data:text/html;", 0) == 0) {
                                    // is triggered by loadUrl() or loadHtmlString(), not user initiated
                                    // or is triggered by goBack / goForward
                                    // then we don't ask client Dart code (onNavigationRequest) to allow/prevent loading url
                                    nowLoadingUrl = L"";
                                    m_isNowGoBackForward = false;
                                    userInitiated = false;
                                }

                                onPageStarted(utf8Url, false, userInitiated);

                                // always cancel user initiated navigation, and pass this event to [webview_flutter]
                                // and after [webview_flutter] ask client dart code, if client say yes,
                                // [webview_flutter] then call loadUrl() to load url again
                                if (userInitiated && m_hasNavigationDecision) args->put_Cancel(TRUE);
                            }
                            return S_OK;
                        }).Get(), NULL);

                m_pWebview->add_NewWindowRequested(
                    Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                        [=](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                            wil::unique_cotaskmem_string url;
                            HRESULT hr = args->get_Uri(&url);
                            if (SUCCEEDED(hr)) {
                                auto utf8Url = utf8_encode(std::wstring(url.get()));
                                //bool isAllowed = m_bAllowNewWindow && checkUrlAllowed(utf8Url); //TODO

                                args->put_Handled(TRUE);
                                if (m_hasNavigationDecision) {
                                    onPageStarted(utf8Url, true, true);
                                } else {
                                    loadUrl(url.get());
                                }
                            }

                            //wil::com_ptr<ICoreWebView2Deferral> deferral;
                            //hr = args->GetDeferral(&deferral);
                            //deferral->Complete

                            return S_OK;
                        }).Get(), NULL);

                m_pWebview->add_NavigationCompleted(
                    Callback<ICoreWebView2NavigationCompletedEventHandler>(
                        [=](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                            UINT64 navigationId = 0;
                            HRESULT hr = S_OK;
                            int errCode = 0;
                            BOOL success = FALSE;
                            args->get_IsSuccess(&success);
                            if (!success) {
                                COREWEBVIEW2_WEB_ERROR_STATUS webErrorStatus;
                                hr = args->get_WebErrorStatus(&webErrorStatus);
                                if (SUCCEEDED(hr)) {
                                    errCode = webErrorStatus; // TODO: enum all the error code...
                                }
                            }

                            args->get_NavigationId(&navigationId);
                            std::string url = g_navigationMap[navigationId];
                            g_navigationMap.erase(navigationId);

                            if (errCode != COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) {
                                onPageFinished(url, errCode);
                            }
                            return S_OK;
                        }).Get(), NULL);

                m_pWebview->add_HistoryChanged(
                    Callback<ICoreWebView2HistoryChangedEventHandler>(
                        [=](ICoreWebView2* sender, IUnknown* args) -> HRESULT {
                            if (onHistoryChanged) {
                                onHistoryChanged();
                            }

                            return S_OK;
                        })
                        .Get(), NULL);

                m_pWebview->add_DocumentTitleChanged(
                    Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                        [=](ICoreWebView2* sender, IUnknown* args) -> HRESULT {
                            wil::unique_cotaskmem_string pwTitle;
                            HRESULT hr = sender->get_DocumentTitle(&pwTitle);
                            if (SUCCEEDED(hr)) {
                                std::string title = utf8_encode(pwTitle.get());
                                onPageTitleChanged(title);
                            }
                            return S_OK;
                        }).Get(), NULL);


                hr = m_pWebview->add_WebMessageReceived(
                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                        [=](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                            if (onWebMessageReceived != NULL) {
                                wil::unique_cotaskmem_string json;
                                HRESULT hr = args->get_WebMessageAsJson(&json);
                                if (SUCCEEDED(hr)) {
                                    onWebMessageReceived(Utf8FromUtf16(json.get()));
                                }
                            }
                            return S_OK;
                        }).Get(), NULL); /// &m_webMessageReceivedToken

                hr = m_pController->add_MoveFocusRequested(
                    Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
                        [=](ICoreWebView2Controller* sender, ICoreWebView2MoveFocusRequestedEventArgs* args) -> HRESULT {
                            COREWEBVIEW2_MOVE_FOCUS_REASON reason;
                            args->get_Reason(&reason);
                            onMoveFocusRequest(reason == COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT);
                            return S_OK;
                        }).Get(), NULL);

                hr = m_pWebview->add_ContainsFullScreenElementChanged(
                    Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
                        [=](ICoreWebView2* sender, IUnknown* args) -> HRESULT {
                            BOOL isFullScreen;
                            m_pWebview->get_ContainsFullScreenElement(&isFullScreen);
                            onFullScreenChanged(isFullScreen);
                            return S_OK;
                        })
                    .Get(), nullptr);

                hr = m_pWebview->add_PermissionRequested(
                    Callback<ICoreWebView2PermissionRequestedEventHandler>(
                        [=](ICoreWebView2* sender, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                            askFlutterPermission(args, onAskPermission);                           
                            return S_OK;
                    }).Get(), NULL);

                onCreated(hr, this);
                return hr;
            }).Get());
        });
}

void MyWebViewImpl::askFlutterPermission(wil::com_ptr<ICoreWebView2PermissionRequestedEventArgs> args, OnAskPermissionFunc onAskPermission)
{
    wil::com_ptr<ICoreWebView2Deferral> deferral;
    COREWEBVIEW2_PERMISSION_KIND kind;                           
    wil::unique_cotaskmem_string uri;

    args->get_PermissionKind(&kind);
    args->get_Uri(&uri);
    args->GetDeferral(&deferral);

    int deferralId = ++lastPermissionDeferralId;
    permissionArgsMap[deferralId] = std::pair(args, deferral);
    onAskPermission(utf8_encode(std::wstring(uri.get())), kind, deferralId);
}

void MyWebViewImpl::grantPermission(int deferralId, BOOL isGranted)
{
    auto it = permissionArgsMap.find(deferralId);
    if (it == permissionArgsMap.end()) return; // not found

    auto pair = std::move(it->second);
    permissionArgsMap.erase(it);

    auto args = pair.first;
    auto deferral = pair.second;

    auto state = isGranted ? COREWEBVIEW2_PERMISSION_STATE_ALLOW : COREWEBVIEW2_PERMISSION_STATE_DENY;
    args->put_State(state);
    deferral->Complete();
}

MyWebViewImpl::~MyWebViewImpl()
{
    m_pController->Close();
    std::cout << "MyWebViewImpl::~MyWebViewImpl()\n";
}

void MyWebViewImpl::setHasNavigationDecision(bool hasNavigationDecision)
{
    m_hasNavigationDecision = hasNavigationDecision;
}


HRESULT MyWebViewImpl::loadUrl(LPCWSTR url)
{
    nowLoadingUrl = url;
    return m_pWebview->Navigate(url);
}

HRESULT MyWebViewImpl::loadHtmlString(LPCWSTR html)
{
    return m_pWebview->NavigateToString(html);
}

HRESULT MyWebViewImpl::runJavascript(LPCWSTR javaScriptString, bool ignoreResult, std::function<void(std::string)> callback)
{
    return m_pWebview->ExecuteScript(javaScriptString, Callback<ICoreWebView2ExecuteScriptCompletedHandler >(
        [callback, ignoreResult](HRESULT hr, LPCWSTR resultObjectAsJson) -> HRESULT {
            if (callback != nullptr) {
                if (ignoreResult) callback("");
                else callback(utf8_encode(resultObjectAsJson));
            }
            return hr;
        }).Get());
}

HRESULT MyWebViewImpl::addScriptChannelByName(LPCWSTR channelName)
{
    if (!m_hasRegisteredChannel) {
        m_hasRegisteredChannel = true;

        LPCWSTR script = L"class JkChannel { constructor(name) { this.name = name; } postMessage(message) { window.chrome.webview.postMessage({'JkChannelName': this.name, 'msg' : message}); } }";
        HRESULT hr = m_pWebview->AddScriptToExecuteOnDocumentCreated(script, Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [](HRESULT error, PCWSTR id) -> HRESULT {
                return S_OK; //do nothing
            }).Get());
        if (FAILED(hr)) return E_FAIL;
    }

    WCHAR script[100];
    if (wcslen(channelName) > 30) return E_FAIL;
    wsprintf(script, L"const %s = new JkChannel('%s');", channelName, channelName);

    return m_pWebview->AddScriptToExecuteOnDocumentCreated(script, Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
        [=](HRESULT error, PCWSTR id) -> HRESULT {
            if (FAILED(error)) return error;
            channelMap[channelName] = id;
            return S_OK; //do nothing
        }).Get());
}

void MyWebViewImpl::removeScriptChannelByName(LPCWSTR channelName)
{
    std::wstring key = channelName;
    if (channelMap.find(key) != channelMap.end())
    {
        std::wstring id = channelMap[key];
        m_pWebview->RemoveScriptToExecuteOnDocumentCreated(id.c_str());
        channelMap.erase(key);
    }
}

HRESULT MyWebViewImpl::updateBounds(RECT& bounds)
{
    m_bounds = bounds;
    return m_pController->put_Bounds(bounds);
}

HRESULT MyWebViewImpl::getBounds(RECT& bounds)
{
    bounds = m_bounds;
    return S_OK;
}

HRESULT MyWebViewImpl::setVisible(bool isVisible)
{
    return m_pController->put_IsVisible(isVisible);
}

HRESULT MyWebViewImpl::setBackgroundColor(int32_t argb)
{
    COREWEBVIEW2_COLOR value;
    value.R = GetBValue(argb);
    value.G = GetGValue(argb);
    value.B = GetRValue(argb);
    value.A = 255;
    wil::com_ptr<ICoreWebView2Controller2> controller2 = m_pController.query<ICoreWebView2Controller2>();
    return controller2->put_DefaultBackgroundColor(value);
}

HRESULT MyWebViewImpl::requestFocus(bool isNext)
{
    m_pController->MoveFocus(isNext ? COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT : COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS);
    return S_OK;
}

void MyWebViewImpl::enableJavascript(bool bEnable)
{
    m_pSettings->put_IsScriptEnabled(bEnable);
}

void MyWebViewImpl::enableStatusBar(bool bEnable)
{
    m_pSettings->put_IsStatusBarEnabled(bEnable);
}

void MyWebViewImpl::enableIsZoomControl(bool bEnable)
{
    m_pSettings->put_IsZoomControlEnabled(bEnable);
}

HRESULT MyWebViewImpl::setUserAgent(LPCWSTR userAgent)
{
    wil::com_ptr<ICoreWebView2Settings2> pSettings2;
    HRESULT hr = m_pSettings->QueryInterface(&pSettings2);
    if (SUCCEEDED(hr)) {
        hr = pSettings2->put_UserAgent(userAgent);
        return hr;
    }
    return E_FAIL;
}

bool MyWebViewImpl::canGoBack()
{
    BOOL value = FALSE;
    m_pWebview->get_CanGoBack(&value);
    return value;
}

bool MyWebViewImpl::canGoForward()
{
    BOOL value = FALSE;
    m_pWebview->get_CanGoForward(&value);
    return value;
}

void MyWebViewImpl::goBack()
{
    m_isNowGoBackForward = true;
    m_pWebview->GoBack();
}

void MyWebViewImpl::goForward()
{
    m_isNowGoBackForward = true;
    m_pWebview->GoForward();
}

void MyWebViewImpl::reload()
{
    m_pWebview->Reload();
}

void MyWebViewImpl::cancelNavigate()
{
    m_pWebview->Stop();
}

template<class T> wil::com_ptr<T> MyWebViewImpl::getProfile() {
    static_assert(std::is_base_of<ICoreWebView2Profile, T>::value, "T must inherit from <ICoreWebView2Profile>");
    wil::com_ptr<ICoreWebView2Profile> pProfile;

    auto pWebView_13 = m_pWebview.try_query<ICoreWebView2_13>();
    if (pWebView_13 != NULL) {
        pWebView_13->get_Profile(&pProfile);
    }

    if (pProfile == NULL) return wil::com_ptr<T>();
    return pProfile.try_query<T>();
}

HRESULT MyWebViewImpl::clearCache()
{
    HRESULT hr = E_FAIL;
    auto pProfile_2 = getProfile<ICoreWebView2Profile2>();
    if (pProfile_2 != NULL) {
        hr = pProfile_2->ClearBrowsingDataAll(NULL);
    }
    return hr;
}

HRESULT MyWebViewImpl::clearCookies()
{
    wil::com_ptr<ICoreWebView2CookieManager> cookieManager;
    auto webview2_2 = m_pWebview.try_query<ICoreWebView2_2>();
    if (webview2_2 == NULL) return E_FAIL;

    webview2_2->get_CookieManager(&cookieManager);
    if (cookieManager == NULL) return E_FAIL;

    return cookieManager->DeleteAllCookies();
}

HRESULT MyWebViewImpl::setCookie(LPCWSTR name, LPCWSTR value, LPCWSTR domain, LPCWSTR path)
{
    wil::com_ptr<ICoreWebView2CookieManager> cookieManager;
    auto webview2_2 = m_pWebview.try_query<ICoreWebView2_2>();
    if (webview2_2 == NULL) return E_FAIL;

    webview2_2->get_CookieManager(&cookieManager);
    if (cookieManager == NULL) return E_FAIL;

    wil::com_ptr<ICoreWebView2Cookie> cookie;
    HRESULT hr = cookieManager->CreateCookie(name, value, domain, path, &cookie);
    if (FAILED(hr)) return hr;

    return cookieManager->AddOrUpdateCookie(cookie.get());
}

HRESULT MyWebViewImpl::getCookies(LPCWSTR uri, std::function<void(std::string)> callback)
{
    wil::com_ptr<ICoreWebView2CookieManager> cookieManager;
    auto webview2_2 = m_pWebview.try_query<ICoreWebView2_2>();
    if (webview2_2 == NULL) return E_FAIL;

    webview2_2->get_CookieManager(&cookieManager);
    if (cookieManager == NULL) return E_FAIL;

    return cookieManager->GetCookies(uri, 
        Callback<ICoreWebView2GetCookiesCompletedHandler>(
            [callback](HRESULT error, ICoreWebView2CookieList* cookieList) -> HRESULT {
                if (FAILED(error)) {
                    callback("[]");
                    return error;
                }
                
                // Convert cookie list to JSON string
                UINT count = 0;
                cookieList->get_Count(&count);
                
                std::string result = "[";
                for (UINT i = 0; i < count; i++) {
                    wil::com_ptr<ICoreWebView2Cookie> cookie;
                    cookieList->GetValueAtIndex(i, &cookie);
                    
                    wil::unique_cotaskmem_string name, value, domain, path;
                    cookie->get_Name(&name);
                    cookie->get_Value(&value);
                    cookie->get_Domain(&domain);
                    cookie->get_Path(&path);
                    
                    if (i > 0) result += ",";
                    result += "{\"name\":\"" + utf8_encode(name.get()) + 
                             "\",\"value\":\"" + utf8_encode(value.get()) +
                             "\",\"domain\":\"" + utf8_encode(domain.get()) +
                             "\",\"path\":\"" + utf8_encode(path.get()) + "\"}";
                }
                result += "]";
                
                callback(result);
                return S_OK;
            }).Get());
}

HRESULT MyWebViewImpl::suspend()
{
    auto webview2_3 = m_pWebview.try_query<ICoreWebView2_3>();
    if (webview2_3 == NULL) return E_FAIL;
    return webview2_3->TrySuspend(Callback<ICoreWebView2TrySuspendCompletedHandler>(
        [=](HRESULT errorCode, BOOL isSuccessful) -> HRESULT {
            return S_OK;
        }).Get());
}

HRESULT MyWebViewImpl::resume()
{
    auto webview2_3 = m_pWebview.try_query<ICoreWebView2_3>();
    if (webview2_3 == NULL) return E_FAIL;
    return webview2_3->Resume();
}

void MyWebViewImpl::openDevTools()
{
    m_pWebview->OpenDevToolsWindow();
}