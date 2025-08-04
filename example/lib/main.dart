import 'dart:io';

import 'package:flutter/material.dart';
import 'package:webview_flutter/webview_flutter.dart';
import 'package:webview_win_floating/webview_win_floating.dart';

//typedef WebViewController = WinWebViewController;
//typedef WebViewWidget = WinWebViewWidget;
//typedef NavigationDelegate = WinNavigationDelegate;

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({Key? key}) : super(key: key);

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  final controller = WebViewController(onPermissionRequest: (request) {
    if (Platform.isWindows) {
      late final WinWebViewPermissionRequest req;
      if (request is WinWebViewPermissionRequest) {
        req = request as WinWebViewPermissionRequest;
      } else {
        req = request.platform as WinWebViewPermissionRequest;
      }
      print("permission: ${req.kind} , ${req.url}");
    } else {
      return;
    }

    request.grant();
    //req.deny();
  });
  final urlController = TextEditingController();

  @override
  void initState() {
    super.initState();
    controller.setJavaScriptMode(JavaScriptMode.unrestricted);
    controller.setBackgroundColor(Colors.cyanAccent);

    controller.setNavigationDelegate(NavigationDelegate(
      /*
      onNavigationRequest: (request) {
        if (request.url.startsWith("https://www.bnext.com.tw")) {
          return NavigationDecision.navigate;
        } else {
          print("prevent user navigate out of google website: ${request.url}");
          return NavigationDecision.prevent;
        }
      },
      */
      onPageStarted: (url) {
        urlController.text = url;
        print("onPageStarted: $url");
      },
      onPageFinished: (url) => print("onPageFinished: $url"),
      onWebResourceError: (error) => print("onWebResourceError: ${error.description}"),
    ));

    controller.addJavaScriptChannel("Flutter", onMessageReceived: (message) {
      print("js -> dart : ${message.message}");
    });
    //controller.loadRequest(Uri.parse("https://www.google.com"));
    controller.loadRequest(Uri.parse("https://www.bennish.net/web-notifications.html")); // javascript notification test
    //controller.loadRequest(Uri.parse("https://www.bnext.com.tw"));
    //controller.loadRequest(Uri.parse("https://www.w3schools.com/tags/tryit.asp?filename=tryhtml_a_target"));
  }

  void testJavascript() {
    controller.runJavaScript("Flutter.postMessage('Chinese 中文')");
  }

  void testSetCookie() async {
    try {
      // Method 1: Using the controller directly (Windows-specific)
      if (controller is WinWebViewController) {
        final winController = controller as WinWebViewController;
        bool success = await winController.setCookie(
            'test_cookie', 'test_value_${DateTime.now().millisecondsSinceEpoch}', '.bennish.net', '/');
        print("Set cookie using controller: $success");
      }

      // Method 2: Using WebViewCookieManager (cross-platform)
      final cookieManager = WebViewCookieManager();
      await cookieManager.setCookie(WebViewCookie(
        name: 'flutter_cookie',
        value: 'flutter_value_${DateTime.now().millisecondsSinceEpoch}',
        domain: '.bennish.net',
        path: '/',
      ));
      print("Set cookie using WebViewCookieManager");

      // Reload to see cookies in action
      controller.reload();
    } catch (e) {
      print("Error setting cookies: $e");
    }
  }

  void testGetCookies() async {
    try {
      if (controller is WinWebViewController) {
        final winController = controller as WinWebViewController;
        final cookies = await winController.getCookies("https://www.bennish.net");
        print("Current cookies: $cookies");

        // Show cookies in a dialog
        if (mounted) {
          showDialog(
            context: context,
            builder: (context) => AlertDialog(
              title: Text("Cookies"),
              content: Text(cookies.isNotEmpty
                  ? cookies.map((c) => "${c['name']}: ${c['value']}").join('\n')
                  : "No cookies found"),
              actions: [
                TextButton(
                  onPressed: () => Navigator.pop(context),
                  child: Text("OK"),
                ),
              ],
            ),
          );
        }
      }
    } catch (e) {
      print("Error getting cookies: $e");
    }
  }

  @override
  Widget build(BuildContext context) {
    Widget urlBox = TextField(
      controller: urlController,
      onSubmitted: (url) {
        url = url.trim();
        if (!url.startsWith("http")) {
          url = "https://$url";
        }
        controller.loadRequest(Uri.parse(url));
      },
    );
    Widget buttonRow = Row(children: [
      MyCircleButton(icon: Icons.javascript, onTap: testJavascript),
      MyCircleButton(icon: Icons.cookie, onTap: testSetCookie),
      MyCircleButton(icon: Icons.list, onTap: testGetCookies),
      MyCircleButton(icon: Icons.arrow_back, onTap: controller.goBack),
      MyCircleButton(icon: Icons.arrow_forward, onTap: controller.goForward),
/*
      MyCircleButton(
          icon: Icons.arrow_back,
          onTap: () {
            controller.runJavaScript("history.back();");
          }),
      MyCircleButton(
          icon: Icons.arrow_forward,
          onTap: () {
            controller.runJavaScript("history.forward();");
          }),
*/
      MyCircleButton(icon: Icons.refresh, onTap: controller.reload),
      Expanded(child: urlBox),
    ]);

    Widget body = Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      buttonRow,
      Expanded(child: WebViewWidget(controller: controller)),
    ]);

    return MaterialApp(home: Scaffold(body: body));
  }
}

class MyCircleButton extends StatelessWidget {
  final GestureTapCallback? onTap;
  final IconData icon;
  final double size;

  const MyCircleButton({super.key, required this.onTap, required this.icon, this.size = 32});

  @override
  Widget build(BuildContext context) {
    return ClipOval(
      child: Material(
        color: Colors.blue, // Button color
        child: InkWell(
          splashColor: Colors.red, // Splash color
          onTap: onTap,
          child: SizedBox(width: size, height: size, child: Icon(icon)),
        ),
      ),
    );
  }
}
