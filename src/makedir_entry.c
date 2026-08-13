#include <dos/dos.h>

int ace_makedir_main(void);
void native_publish_result(int result_code);

int main(void)
{
    int result = ace_makedir_main();
    native_publish_result(result);
    return result;
}
