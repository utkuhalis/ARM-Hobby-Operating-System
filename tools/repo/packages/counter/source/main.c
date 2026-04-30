#include <hobby_sdk.h>

int hobby_main(void) {
    hobby_write("[counter] starting\n");
    for (int i = 1; i <= 5; i++) {
        char num[24];
        hobby_itoa(num, i);
        hobby_write("[counter] ");
        hobby_write(num);
        hobby_write("\n");
        for (int y = 0; y < 60; y++) hobby_yield();
    }
    hobby_write("[counter] done\n");
    return 0;
}
