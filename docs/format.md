# AI-first source code formatting

The formatting leaves minimal freedom to how the code is formtted. All the decision on line breaks, indentation, and groupings are done by an automated formatting tool. The only way to affect formatting is by adding comments.

## General rules

- Vertical alignment is never used.
- Always use fixed indent (4 spaces) for next logical group. Tabs are never used.
- Line limit is observed (120 space) and formatter tries to pack lines try to pack to this limit as much as possible. The limit is hard. Only long string constants and comments can cause it to be exceeded. Formatter never breaks constants and comments.
- Groups (classes, method impl, etc) are separated by one empty lines, everything else goes without empty line. When clear separation of parts is needed, author shall use comments. 
- Always line break after opening braces ('{') that start code block with the only exception of empyty pair `{}`. Initilizer-open brace dont hard break line.
- Always line break after closing brace ('}') of the code blocks.
- Always line break after semicolon (';').

## Breaking structures:

Long list inisde braces, brackets, template params are either all a one line, or all on different lines, with inert part using one more indent and opening and closing being one separate lines. 

```
funCall(a, b, c);
veryLongFuntionCall(
    veryLongAraA, 
    veryLongAraB, 
    veryLongAraC
);
vector<int> x{a, b, c};
vector<int> longName{
   longExprA,
   longExprB,
   longExprC
};
```

Same goes for chains of same-priority operators ('+'/'-'), and ternary operator chains:

```
int x = a + b + c;
int longName = longExprA +
    longExprB +
    longExprC;
bool flag = longExprA ? one : two
    longExprB ? thee : four;
    longExprC ? five : six;
```

Indent goes strucuturally inside, depending on the presence of the outer braces, similar example becomes (becuase of the extra brace that pair, the inner chain can be fully aligned):

```
bool flag = funcCall(
    longExprA ? one : two
    longExprB ? thee : four;
    longExprC ? five : six;
);
```

## Configuration:
- Confiugration contains project-specific definitions and magic constants: macro names, max line width, indent width in spaces.
- There are not "formatting style" options in configuration. Only magic constant. The logic is hardcoded and fixed.
