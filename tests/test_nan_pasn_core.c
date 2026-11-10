/* Shared cache/identity API and storage adapters; no radio traffic. */
#include <assert.h>
#include <sys/stat.h>
#include "../app/wifi_nan_utility/pasn_probe/core/green2_pasn.c"

struct fixture {
    unsigned writes, reads, logs;
    int fail;
    uint8_t npk[36], nik[500];
};
static int fixture_write(void *context,enum green2_pasn_store store,const void *data,size_t size)
{
    struct fixture *f=context;f->writes++;
    if(f->fail||store!=GREEN2_PASN_NPK||size!=sizeof(f->npk))return -1;
    memcpy(f->npk,data,size);return 0;
}
static int fixture_read(void *context,enum green2_pasn_store store,void *data,size_t size)
{
    struct fixture *f=context;f->reads++;
    if(f->fail||store!=GREEN2_PASN_NIK_CACHE||size!=sizeof(f->nik))return -1;
    memcpy(data,f->nik,size);return 0;
}
static void fixture_log(void *context,int level,const char *message)
{
    struct fixture *f=context;assert(level==1&&!strcmp(message,"test message\n"));f->logs++;
}
int main(void)
{
    assert(!green2_pasn_crypto_selftest());
    struct fixture fixture={0};struct green2_pasn engine={0};
    engine.bridge.context=&fixture;engine.bridge.write_store=fixture_write;
    engine.bridge.read_store=fixture_read;engine.bridge.log=fixture_log;
    uint8_t key[32],copy[500];memset(key,0x47,sizeof(key));fixture.nik[0]=1;
    assert(!green2_pasn_verification_key(&engine,key));
    assert(fixture.writes==1&&!memcmp(fixture.npk,"NPK1",4)&&!memcmp(fixture.npk+4,key,32));
    assert(!read_store(&engine.bridge,GREEN2_PASN_NIK_CACHE,copy,sizeof(copy)));
    assert(fixture.reads==1&&!memcmp(copy,fixture.nik,sizeof(copy)));
    fixture.fail=1;assert(green2_pasn_verification_key(&engine,key));
    assert(read_store(&engine.bridge,GREEN2_PASN_NIK_CACHE,copy,sizeof(copy)));
    pasn_log(&engine.bridge,1,"test %s\n","message");assert(fixture.logs==1);

    const char *temp=getenv("TMPDIR");if(!temp||!*temp)temp="/tmp";
    char dir[PATH_MAX],path[PATH_MAX];
    assert(snprintf(dir,sizeof(dir),"%s/green2-pasn-core-XXXXXX",temp)<(int)sizeof(dir));
    assert(mkdtemp(dir));assert(snprintf(path,sizeof(path),"%s/npk",dir)<(int)sizeof(path));
    struct green2_pasn files={0};files.bridge.npk_path=path;
    assert(!green2_pasn_verification_key(&files,key));struct stat st;
    assert(!stat(path,&st)&&(st.st_mode&0777)==0600&&st.st_size==36);
    uint8_t record[36];assert(!read_store(&files.bridge,GREEN2_PASN_NPK,record,sizeof(record)));
    assert(!memcmp(record,fixture.npk,sizeof(record)));
    FILE *file=fopen(path,"ab");assert(file);assert(fputc(0,file)!=EOF);assert(!fclose(file));
    assert(read_store(&files.bridge,GREEN2_PASN_NPK,record,sizeof(record)));
    key[0]^=1;assert(!green2_pasn_verification_key(&files,key));
    assert(!read_store(&files.bridge,GREEN2_PASN_NPK,record,sizeof(record))&&!memcmp(record+4,key,32));
    assert(!unlink(path));assert(!rmdir(dir));
    assert(!green2_pasn_create_config(NULL));
    struct green2_pasn_config invalid={.interface_name="lo",.frequency=1};
    assert(!green2_pasn_create_config(&invalid));
    puts("PASN shared core: crypto, identity, callbacks and atomic files PASS");return 0;
}
