#include <hobby_sdk.h>

int hobby_main(void) {
    hobby_write("[hello] Hello, World! (loaded from disk)\n");
    char num[24];
    hobby_itoa(num, hobby_getpid());
    hobby_write("[hello] my pid is ");
    hobby_write(num);
    hobby_write("\n[hello] exiting\n");
    return 0;
}
