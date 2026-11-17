#include <cstdlib>
#include <fstream>
int main(int argc,char **argv) {
    if(argc<2)return 1;
    std::ofstream out(argv[1]);
    for(const char *key:{"LD_PRELOAD","LD_LIBRARY_PATH","QT_PLUGIN_PATH","QT_QPA_PLATFORM","PATH","TMPDIR"}) {
        const char *value=std::getenv(key);out<<key<<"="<<(value?value:"")<<"\n";
    }
    for(int i=2;i<argc;++i)out<<"ARG="<<argv[i]<<"\n";
    return out?0:1;
}
