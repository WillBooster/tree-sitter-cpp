#if SELECT == 0
int value=0;
#  elifdef FEATURE
int value=1;
#	elifndef FEATURE
int value=2;
#else
int value=3;
#endif
int elifdef=4, elifndef=5;
const char* directiveText="#elifdef #elifndef";
// #elifdef #elifndef remain comment text.
int main(){return value+elifdef+elifndef+(directiveText[0]=='#');}
