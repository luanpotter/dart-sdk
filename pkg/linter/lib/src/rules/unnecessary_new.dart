// Copyright (c) 2018, the Dart project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/dart/ast/token.dart';
import 'package:analyzer/dart/ast/visitor.dart';

import '../analyzer.dart';

const _desc = r'Unnecessary new keyword.';

const _details = r'''
**AVOID** new keyword to create instances.

**BAD:**
```dart
class A { A(); }
m(){
  final a = new A();
}
```

**GOOD:**
```dart
class A { A(); }
m(){
  final a = A();
}
```

''';

class UnnecessaryNew extends LintRule {
  static const LintCode code = LintCode(
      'unnecessary_new', "Unnecessary 'new' keyword.",
      correctionMessage: "Try removing the 'new' keyword.",
      hasPublishedDocs: true);

  UnnecessaryNew()
      : super(
            name: 'unnecessary_new',
            description: _desc,
            details: _details,
            categories: {Category.languageFeatureUsage, Category.style});

  @override
  bool get canUseParsedResult => true;

  @override
  LintCode get lintCode => code;

  @override
  void registerNodeProcessors(
      NodeLintRegistry registry, LinterContext context) {
    var visitor = _Visitor(this);
    registry.addInstanceCreationExpression(this, visitor);
  }
}

class _Visitor extends SimpleAstVisitor {
  final LintRule rule;
  _Visitor(this.rule);

  @override
  void visitInstanceCreationExpression(InstanceCreationExpression node) {
    if (node.keyword?.type == Keyword.NEW) {
      rule.reportLintForToken(node.keyword);
    }
  }
}
