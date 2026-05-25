import 'package:flutter/material.dart';
import 'pages/home_page.dart';

void main() {
  runApp(const WatchApp());
}

class WatchApp extends StatelessWidget {
  const WatchApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'SmartBracelet',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: const Color(0xFF0d0d1a),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF00d4ff),
        ),
      ),
      home: const HomePage(),
    );
  }
}
