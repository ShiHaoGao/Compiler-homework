extern int GET();
extern void * MALLOC(int);
extern void FREE(void *);
extern void PRINT(int);

int b=10;
//int f(int x) {
//  if (x > 0)
//  	return 5 + f(x - 5);
//  else
//    return 0;
//}

int f(int x) {
    int a;
    a=100;
    PRINT(a);
    return a;
}

int main() {
   PRINT(f(b));
}