#include <stdio.h>
#include <string.h>
#include "Regexp.h"
int main(){ const char* pat="^[0-9A-Z]?[A-Z]?[0-9]+[A-Z][A-Z]?[A-Z]?[%-]?[0-9]?[0-9]?$";
 const char* t[]={"0A123456789012ABC-99","OE7XWT-12","DK5EN-98","A1111111111111111111"};
 for(auto s: t){ MatchState ms; ms.Target((char*)s); printf("%-22s len %2zu match %d\n", s, strlen(s), ms.Match((char*)pat)>0); } }
