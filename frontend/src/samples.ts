export const SAMPLES: { name: string; code: string }[] = [
  {
    name: "Demo: many problems",
    code: `#include <stdio.h>
#define LIMIT 10

int compute(int n) {
    int unused = 42;
    int zero = 0;
    int result = n / zero;
    if (n > LIMIT) {
        return result;
    }
}

int main() {
    int total;
    int count = 0;
    while (1) {
        count++;
    }
    printf("%d\\n", total);
    return 0;
}
`,
  },
  {
    name: "Division by zero",
    code: `int main() {
    int a = 20 / 0;
    int zero = 0;
    int b = a / zero;
    return b;
}
`,
  },
  {
    name: "Unreachable code",
    code: `int main() {
    int x = 1;
    return x;
    x = 2;
}
`,
  },
  {
    name: "Uninitialized variable",
    code: `int main() {
    int x;
    int y = x + 1;
    int z;
    if (y > 0) {
        z = 1;
    }
    return y + z;
}
`,
  },
  {
    name: "Null pointer",
    code: `#include <stdlib.h>
int main() {
    int *p = NULL;
    *p = 5;
    int *q = malloc(sizeof(int));
    *q = 3;
    return 0;
}
`,
  },
  {
    name: "Syntax errors",
    code: `int main() {
    int x = ;
    int y = 5
    return y;
}
`,
  },
  {
    name: "Semantic errors",
    code: `int main() {
    int x = 1;
    int x = 2;
    y = 3;
    x = "hello";
    foo(1);
    return 0;
}
`,
  },
  {
    name: "Format string errors",
    code: `#include <stdio.h>

int main() {
    int count = 5;
    double avg = 2.5;
    float f;
    printf("%\\n", count);
    printf("%d %d\\n", count);
    printf("%d\\n", avg);
    printf("%s\\n", count);
    scanf("%d", count);
    scanf("%d", &f);
    return 0;
}
`,
  },
  {
    name: "Syntax + logic errors together",
    code: `#include <stdio.h>

int main() {
    int count = 5;
    int bad = ;
    printf("%d\\n", count)
    count == 3;
    return 0;
}
`,
  },
  {
    name: "Memory and loop bugs",
    code: `#include <stdio.h>
#include <stdlib.h>

int main() {
    int arr[5];
    for (int i = 0; i <= 5; i++) { arr[i] = i; }
    for (int j = 0; j < 10; j--) { printf("%d\\n", j); }
    int *p;
    *p = 3;
    int *q = malloc(sizeof(int));
    free(q);
    *q = 2;
    return 0;
}
`,
  },
  {
    name: "Clean program",
    code: `#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

int main() {
    int total = add(2, 3);
    for (int i = 0; i < 3; i++) {
        total += i;
    }
    printf("%d\\n", total);
    return 0;
}
`,
  },
];
