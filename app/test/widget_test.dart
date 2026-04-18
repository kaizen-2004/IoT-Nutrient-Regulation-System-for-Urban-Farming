import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:vertifarm_app/main.dart';

void main() {
  testWidgets('shows onboarding screen when no device is saved', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});

    await tester.pumpWidget(const VertiFarmApp());
    await tester.pumpAndSettle();

    expect(find.text('Add Controller'), findsOneWidget);
    expect(find.text('Scan device QR'), findsOneWidget);
  });
}
