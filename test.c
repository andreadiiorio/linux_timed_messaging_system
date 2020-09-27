static int a,b,c;
#define GETNAME(var)	get_sys_##var
#define basicF(var) \
	void GETNAME(var) (){ \
		var=1; \
	}

int main(){
	basicF(a);GETNAME(a)();
	basicF(c);GETNAME(c)();
	//printf(" a=%d,b=%d,c=%d\n",a,b,c);
	return 0;
}
