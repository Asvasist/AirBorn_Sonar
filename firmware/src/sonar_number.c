#include "sonar_number.h"
#include <stdlib.h>
#include <errno.h>
sonar_number_result_t sonar_number_feed(sonar_number_t *n, uint8_t key, int32_t *value)
{
    if (key=='\r' || key=='\n') {
        n->text[n->length]='\0'; errno=0; char *end;
        long v=strtol(n->text,&end,10);
        bool valid=n->length!=0 && !n->invalid && errno==0 && *end=='\0' && v>=INT32_MIN && v<=INT32_MAX;
        if (valid) { *value=(int32_t)v; }
        *n=(sonar_number_t){0}; return valid?NUMBER_OK:NUMBER_INVALID;
    }
    if (key==8U || key==127U) { if (n->length>0) { --n->length; } }
    else if ((key>='0' && key<='9') || (key=='-' && n->length==0)) {
        if (n->length<sizeof(n->text)-1U) { n->text[n->length++]=(char)key; }
        else { n->invalid=true; }
    } else { n->invalid=true; }
    return NUMBER_WAIT;
}
