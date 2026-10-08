---
description: OpenStreetMap rendering via Belend2D and SDL3
globs: "**/*"
alwaysApply: false
---

# Code style
- Use 4 spaces indentation (for C, C++, and Ada code)
- Functions code open with curly brace on the a line:
```cpp
void func()
{
    // code
}
```

- Classes and struct definition curly braces start on a new line:
```cpp
class Map
{
public:
    Map();

protected:

private:
    std::string m_name;
};
```

In classes the sections should be in this order: `public`, `protected`, `private`.

- Class member variables have `m_` prefix.
- Members of public structures should not have any prefix.
- Identifiers use CamelCase naming style, with UpperCamelCase for types, and lowerCamelCase for variables.

- All other code scopes have curly brase put at the end of the statement line:
```cpp
if (condition) {
    doSomething();

    while (anotherCondition) {
        doSomethingElse();
    }
}
```

# Code structure
- Put code elaboration in `*.cpp` files, rather than `*.h` if possible.
- Back the code with unit tests when reasonable.
