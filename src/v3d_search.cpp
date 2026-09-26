#include <vulkan/vulkan.h>
#include "ap27_v3d/small_primes.hpp"
#include "ap27_v3d/v3d_search.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <time.h>
#include <unistd.h>

static constexpr uint64_t MOD=258559632607830ULL;
static constexpr uint64_t PRIM23=223092870ULL;
static constexpr uint32_t SEEDS=32, PER_SEED=12673;
static constexpr uint32_t N59_COUNT=SEEDS*PER_SEED, CAP=1000000;
static_assert(MOD < (1ULL<<48)); // Sieve record bit 48 tags the second shift.
static constexpr uint64_t N0=106990415896110ULL, N30=94805198622871ULL;
static constexpr uint64_t S3=172373088405220ULL, S5=51711926521566ULL;
static constexpr uint64_t PRES2=16681266619860ULL, PRES3=181690552643340ULL;
static constexpr uint64_t PRES4=132432982555230ULL, PRES5=126273308948010ULL;
static constexpr uint64_t PRES6=115526644356690ULL, PRES7=48784836341100ULL;
static constexpr uint64_t PRES8=100794433050510ULL;
static constexpr uint32_t SMALL[]={7,11,13,17,19,23};

void check(VkResult r,const char* what) { if(r!=VK_SUCCESS)throw std::runtime_error(std::string(what)+": VkResult "+std::to_string(r)); }
uint64_t process_ns() { timespec t{}; clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t); return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec; }
uint64_t pack(uint32_t lo,uint32_t hi) { return uint64_t(lo)|(uint64_t(hi)<<32); }
struct U2 { uint32_t lo,hi; };
U2 split(uint64_t x) { return {uint32_t(x),uint32_t(x>>32)}; }
struct U4 { uint32_t x,y,z,w; };
struct SeedData { U2 seeds[32]; U2 offsets[71]; };
struct Push { uint32_t s59lo,s59hi,steplo,stephi,shift,count,cap,reserved; };
struct SieveRec { uint64_t n, mask; bool operator<(const SieveRec& b) const {return std::tie(n,mask)<std::tie(b.n,b.mask);} bool operator==(const SieveRec& b) const {return n==b.n&&mask==b.mask;} };
struct Outcome { uint64_t n,first; uint32_t count,flags; bool operator<(const Outcome& b) const {return std::tie(n,count,first,flags)<std::tie(b.n,b.count,b.first,b.flags);} bool operator==(const Outcome& b) const {return n==b.n&&count==b.count&&first==b.first&&flags==b.flags;} };
uint64_t residue(uint64_t pres,uint32_t K) { return (pres*(K%17835)+((pres*17835)%MOD)*(K/17835))%MOD; }
uint64_t mulmod(uint64_t a,uint64_t b,uint64_t n){return uint64_t((__uint128_t(a)*b)%n);}
uint64_t powmod(uint64_t a,uint64_t e,uint64_t n){uint64_t r=1%n;for(;e;e>>=1,a=mulmod(a,a,n))if(e&1)r=mulmod(r,a,n);return r;}
bool strong_prp_cpu(uint64_t n){
    if(n<3 || !(n&1)) throw std::runtime_error("non-odd PRP modulus");
    uint64_t d=n-1; unsigned t=__builtin_ctzll(d); d>>=t;
    uint64_t a=powmod(2,d,n); if(a==1||a==n-1)return true;
    for(unsigned i=1;i<t;i++){a=mulmod(a,a,n);if(a==n-1)return true;}
    return false;
}
Outcome check_candidate(uint64_t n,uint64_t step){
    uint64_t m=n+5*step; if(m<n) throw std::runtime_error("CPU forward overflow");
    uint32_t k=0,flags=0;
    while(strong_prp_cpu(m)){
        flags=1; k++; m+=step;
        if(m<n) throw std::runtime_error("CPU forward overflow");
    }
    uint64_t first=0;
    if(k>=10){
        m=n+4*step; uint64_t start=m;
        while(strong_prp_cpu(m)){m-=step;k++;if(m>start)break;}
        first=m+step; flags|=2;
    }
    return {n,first,k,flags};
}
SeedData make_seed_offsets(uint32_t K){
    SeedData d{};
    const uint64_t s43=residue(PRES5,K),s47=residue(PRES6,K),s53=residue(PRES7,K);
    for(uint32_t i=0;i<19;i++)d.offsets[i]=split((i*s43)%MOD);
    for(uint32_t i=0;i<23;i++)d.offsets[19+i]=split((i*s47)%MOD);
    for(uint32_t i=0;i<29;i++)d.offsets[42+i]=split((i*s53)%MOD);
    return d;
}
void fill_seed_tile(SeedData& d,const uint64_t* n43,uint32_t base,uint32_t count){
    for(uint32_t i=0;i<count;i++)d.seeds[i]=split(n43[base+i]);
}
std::vector<uint64_t> generate_cpu(const SeedData& seeds,uint32_t K,uint32_t count){
    std::vector<uint64_t> n(count*PER_SEED);
    const uint64_t s43=residue(PRES5,K),s47=residue(PRES6,K),s53=residue(PRES7,K);
    uint32_t idx=0;
    for(uint32_t seed=0;seed<count;seed++){
        uint64_t n43=pack(seeds.seeds[seed].lo,seeds.seeds[seed].hi);
        for(uint32_t i43=0;i43<19;i43++){
            uint64_t n47=n43;
            for(uint32_t i47=0;i47<23;i47++){
                uint64_t n53=n47;
                for(uint32_t i53=0;i53<29;i53++){
                    n[idx++]=n53;
                    n53+=s53;if(n53>=MOD)n53-=MOD;
                }
                n47+=s47;if(n47>=MOD)n47-=MOD;
            }
            n43+=s43;if(n43>=MOD)n43-=MOD;
        }
    }
    if(idx!=count*PER_SEED)throw std::runtime_error("CPU setupn count mismatch");
    return n;
}
void fill_masks(std::span<U2,mask_count> masks,uint64_t step,uint32_t shift){
    std::array<uint8_t,small_primes.back().p> ok{};
    for(const auto& prime:small_primes){
        uint32_t p=prime.p; std::fill_n(ok.begin(),p,uint8_t{1});
        for(uint32_t j=p-23;j<=p;j++) ok[(j*(step%p))%p]=0;
        uint32_t modp=MOD%p;
        for(uint32_t i=0;i<p;i++){
            uint64_t bits=0;
            for(uint32_t b=0;b<64;b++){
                uint32_t r=(i+((b+shift)*modp))%p;
                if(ok[r])bits|=1ULL<<b;
            }
            masks[prime.offset+i]=split(bits);
        }
    }
}
std::vector<SieveRec> sieve_cpu(const std::vector<uint64_t>& n59,const std::vector<U2>& masks,uint64_t stride){
    std::vector<SieveRec> out; out.reserve(30000);
    for(auto seed:n59){
        uint64_t n=seed;
        for(uint32_t iter=0;iter<35;iter++){
            uint32_t a=uint32_t(n)&0x3fffffff,b=uint32_t(n>>30);
            uint64_t bits=UINT64_MAX;
            for(auto prime:small_primes){
                const auto& m=masks[prime.offset+(a+prime.coeff*b)%prime.p];
                bits&=pack(m.lo,m.hi);
                if(!bits)break;
            }
            if(bits)out.push_back({n,bits});
            n+=stride; if(n>=MOD)n-=MOD;
        }
    }
    return out;
}
std::vector<uint64_t> compact_cpu(const std::vector<SieveRec>& rec,uint32_t shift){
    std::vector<uint64_t> out;out.reserve(15000);
    for(const auto& r:rec){
        uint64_t bits=r.mask;
        while(bits){
            uint32_t bit=__builtin_ctzll(bits); bits&=bits-1;
            uint64_t n=r.n+(uint64_t(bit)+shift)*MOD;
            bool keep=true;for(uint32_t p:SMALL)if(n%p==0){keep=false;break;}
            if(keep)out.push_back(n);
        }
    }
    return out;
}

std::vector<uint32_t> read_spirv(const std::string& path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("missing shader "+path);
    auto n=f.tellg();if(n<=0||n%4)throw std::runtime_error("bad SPIR-V size");
    f.seekg(0);std::vector<uint32_t>b(size_t(n)/4);
    if(!f.read(reinterpret_cast<char*>(b.data()),n))throw std::runtime_error("cannot read shader "+path);
    return b;
}
uint32_t memory_type(VkPhysicalDevice gpu,uint32_t bits,VkMemoryPropertyFlags flags){
    VkPhysicalDeviceMemoryProperties p;vkGetPhysicalDeviceMemoryProperties(gpu,&p);
    for(uint32_t i=0;i<p.memoryTypeCount;i++)if((bits&(1u<<i))&&(p.memoryTypes[i].propertyFlags&flags)==flags)return i;
    throw std::runtime_error("no host coherent V3D memory");
}
struct Buffer{VkBuffer b{};VkDeviceMemory m{};void* ptr{};VkDeviceSize size{};};
Buffer make_buffer(VkDevice dev,VkPhysicalDevice gpu,VkDeviceSize size,VkBufferUsageFlags usage){
    Buffer out;out.size=size;
    try {
        VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};c.size=size;c.usage=usage|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        check(vkCreateBuffer(dev,&c,nullptr,&out.b),"buffer create");
        VkMemoryRequirements req;vkGetBufferMemoryRequirements(dev,out.b,&req);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;
        a.memoryTypeIndex=memory_type(gpu,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(dev,&a,nullptr,&out.m),"buffer memory");
        check(vkBindBufferMemory(dev,out.b,out.m,0),"buffer bind");
        check(vkMapMemory(dev,out.m,0,size,0,&out.ptr),"buffer map");
        return out;
    } catch (...) {
        if(out.ptr)vkUnmapMemory(dev,out.m);
        if(out.b)vkDestroyBuffer(dev,out.b,nullptr);
        if(out.m)vkFreeMemory(dev,out.m,nullptr);
        throw;
    }
}
void destroy(VkDevice dev,Buffer& b){vkUnmapMemory(dev,b.m);vkDestroyBuffer(dev,b.b,nullptr);vkFreeMemory(dev,b.m,nullptr);}

struct V3D {
    V3D(const V3D&)=delete;
    V3D& operator=(const V3D&)=delete;
    VkInstance instance{}; VkDevice dev{}; VkQueue queue{}; VkPhysicalDevice gpu{};
    VkDescriptorSetLayout dsl{}; VkPipelineLayout layout{}; VkDescriptorPool pool{};
    VkDescriptorSet set{}; VkCommandPool command_pool{}; VkCommandBuffer cb{}; VkFence fence{};
    std::array<Buffer,7> b{}; std::array<VkShaderModule,6> modules{};
    std::array<VkPipeline,6> pipelines{};
    V3D(){
      try {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_2; app.pApplicationName="AP27 BOINC V3D";
        VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ic.pApplicationInfo=&app;
        check(vkCreateInstance(&ic,nullptr,&instance),"instance");
        uint32_t ng=0; check(vkEnumeratePhysicalDevices(instance,&ng,nullptr),"enumerate");
        std::vector<VkPhysicalDevice> gpus(ng); check(vkEnumeratePhysicalDevices(instance,&ng,gpus.data()),"devices");
        for(auto g:gpus){VkPhysicalDeviceProperties p;vkGetPhysicalDeviceProperties(g,&p);
            if(p.vendorID==0x14e4 && std::strstr(p.deviceName,"V3D")){gpu=g;break;}}
        if(!gpu)throw std::runtime_error("V3D unavailable");
        uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,nullptr);
        std::vector<VkQueueFamilyProperties> qp(nq);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,qp.data());
        uint32_t family=nq;for(uint32_t i=0;i<nq;i++)if(qp[i].queueFlags&VK_QUEUE_COMPUTE_BIT){family=i;break;}
        if(family==nq)throw std::runtime_error("V3D compute queue unavailable");
        float priority=1;VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qc.queueFamilyIndex=family;qc.queueCount=1;qc.pQueuePriorities=&priority;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qc;
        check(vkCreateDevice(gpu,&dc,nullptr,&dev),"device");vkGetDeviceQueue(dev,family,0,&queue);
        constexpr std::array<VkDeviceSize,7> sizes={sizeof(SeedData),sizeof(U2)*N59_COUNT,
            sizeof(U2)*mask_count,sizeof(U4)*CAP,sizeof(U2)*CAP,sizeof(U4)*CAP,64};
        for(size_t i=0;i<b.size();++i)
            b[i]=make_buffer(dev,gpu,sizes[i],i==6?VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT:0);
        VkDescriptorSetLayoutBinding lb[7]{};for(uint32_t i=0;i<7;i++){lb[i].binding=i;lb[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;lb[i].descriptorCount=1;lb[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
        VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};lc.bindingCount=7;lc.pBindings=lb;
        check(vkCreateDescriptorSetLayout(dev,&lc,nullptr,&dsl),"descriptor layout");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};
        VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pc.setLayoutCount=1;pc.pSetLayouts=&dsl;pc.pushConstantRangeCount=1;pc.pPushConstantRanges=&range;
        check(vkCreatePipelineLayout(dev,&pc,nullptr,&layout),"pipeline layout");
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,7};VkDescriptorPoolCreateInfo dpc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dpc.maxSets=1;dpc.poolSizeCount=1;dpc.pPoolSizes=&ps;
        check(vkCreateDescriptorPool(dev,&dpc,nullptr,&pool),"descriptor pool");
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&dsl;
        check(vkAllocateDescriptorSets(dev,&da,&set),"descriptor set");
        VkDescriptorBufferInfo info[7]{};VkWriteDescriptorSet writes[7]{};
        for(uint32_t i=0;i<7;i++){info[i]={b[i].b,0,VK_WHOLE_SIZE};writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&info[i];}
        vkUpdateDescriptorSets(dev,7,writes,0,nullptr);
        char path[4096];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
        if(n<0)throw std::runtime_error("cannot locate shader directory");path[n]=0;
        std::string dir(path);dir.resize(dir.find_last_of('/'));
        for(int i=0;i<6;i++){
            auto words=read_spirv(dir+"/stage_"+std::to_string(i)+".spv");
            VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=words.size()*4;sm.pCode=words.data();
            check(vkCreateShaderModule(dev,&sm,nullptr,&modules[i]),"shader module");
            VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.layout=layout;
            cp.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=modules[i];cp.stage.pName="main";
            check(vkCreateComputePipelines(dev,VK_NULL_HANDLE,1,&cp,nullptr,&pipelines[i]),"compute pipeline");
        }
        VkCommandPoolCreateInfo cpc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpc.queueFamilyIndex=family;cpc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(dev,&cpc,nullptr,&command_pool),"command pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=command_pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
        check(vkAllocateCommandBuffers(dev,&ca,&cb),"command buffer");
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(dev,&fc,nullptr,&fence),"fence");
      } catch (...) { destroy_all(); throw; }
    }
    ~V3D(){ destroy_all(); }
    void destroy_all() noexcept {
        if(dev)vkDeviceWaitIdle(dev);
        if(fence)vkDestroyFence(dev,fence,nullptr);if(command_pool)vkDestroyCommandPool(dev,command_pool,nullptr);
        for(int i=0;i<6;i++){if(pipelines[i])vkDestroyPipeline(dev,pipelines[i],nullptr);if(modules[i])vkDestroyShaderModule(dev,modules[i],nullptr);}
        if(pool)vkDestroyDescriptorPool(dev,pool,nullptr);if(layout)vkDestroyPipelineLayout(dev,layout,nullptr);if(dsl)vkDestroyDescriptorSetLayout(dev,dsl,nullptr);
        for(auto& x:b)if(x.b)destroy(dev,x);
        if(dev)vkDestroyDevice(dev,nullptr);if(instance)vkDestroyInstance(instance,nullptr);
    }
    void prepare_pair(uint64_t step,uint32_t shift){
        // The shader rotates each prime's row by 64*MOD mod p for shift+64.
        fill_masks(std::span<U2,mask_count>(static_cast<U2*>(b[2].ptr),mask_count),step,shift);
    }
    void tile_pair(uint32_t K,const SeedData& seeds,uint32_t count,uint32_t shift,std::vector<APHit>& hits){
        std::memcpy(b[0].ptr,&seeds,sizeof(seeds));
        uint64_t step=uint64_t(K)*PRIM23;
        std::memset(b[6].ptr,0,b[6].size);
        uint64_t s59=residue(PRES8,K);uint32_t n59=count*PER_SEED;
        Push push{uint32_t(s59),uint32_t(s59>>32),uint32_t(step),uint32_t(step>>32),shift,n59,CAP,0};
        check(vkResetCommandBuffer(cb,0),"reset command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};check(vkBeginCommandBuffer(cb,&begin),"begin command buffer");
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
        vkCmdPushConstants(cb,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);
        for(uint32_t stage=0;stage<6;stage++){
            vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[stage]);
            if(stage==0)vkCmdDispatch(cb,(n59+63)/64,1,1);
            else if(stage==1)vkCmdDispatch(cb,(n59+511)/512,1,1);
            else if(stage==2||stage==4)vkCmdDispatch(cb,1,1,1);
            else vkCmdDispatchIndirect(cb,b[6].b,(stage==3?4:8)*sizeof(uint32_t));
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask=stage==5?VK_ACCESS_HOST_READ_BIT:(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
            VkPipelineStageFlags dst=stage==5?VK_PIPELINE_STAGE_HOST_BIT:(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,dst,0,1,&barrier,0,nullptr,0,nullptr);
        }
        check(vkEndCommandBuffer(cb),"end command buffer");check(vkResetFences(dev,1,&fence),"reset fence");
        VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};sub.commandBufferCount=1;sub.pCommandBuffers=&cb;
        check(vkQueueSubmit(queue,1,&sub,fence),"queue submit");
        check(vkWaitForFences(dev,1,&fence,VK_TRUE,300000000000ULL),"wait tile fence");
        const uint32_t* control=static_cast<const uint32_t*>(b[6].ptr);
        if(control[2] || control[0]>CAP || control[1]>CAP)throw std::runtime_error("V3D output overflow or invalid PRP arithmetic");
        const U4* status=static_cast<const U4*>(b[5].ptr);
        uint32_t observed_hits=0;
        for(uint32_t i=0;i<control[1];i++)if(status[i].w&2u)
            { hits.push_back({status[i].x,pack(status[i].y,status[i].z)}); observed_hits++; }
        if(observed_hits!=control[3])throw std::runtime_error("V3D hit count mismatch");
    }
};

static std::unique_ptr<V3D> active_v3d;
static bool cpu_backend=false;
void v3d_search_init(){
    const char* backend=std::getenv("AP27_BACKEND");cpu_backend=backend && std::strcmp(backend,"cpu")==0;
    if(!cpu_backend)active_v3d=std::make_unique<V3D>();
    std::fprintf(stderr,"AP27 backend: %s\n",cpu_backend?"CPU reference":"V3D Vulkan");
}
void v3d_search_cleanup(){active_v3d.reset();}
std::vector<APHit> search_ap27_k(unsigned K,unsigned start_shift,const uint64_t* n43,
                                const std::function<void(double)>& tile_done){
    if(!cpu_backend && !active_v3d)throw std::runtime_error("V3D not initialized");
    std::vector<APHit> hits;hits.reserve(256);
    uint64_t step=uint64_t(K)*PRIM23,s59=residue(PRES8,K);
    SeedData seeds=make_seed_offsets(K);
    constexpr uint32_t tiles_per_shift=(10840+SEEDS-1)/SEEDS;
    uint32_t completed=0;
    const char* diagnostic_limit=std::getenv("AP27_DIAGNOSTIC_TILE_LIMIT");
    uint32_t max_tiles=diagnostic_limit?uint32_t(std::strtoul(diagnostic_limit,nullptr,10)):0;
    // Restarts replay an interrupted K, so pairing does not change checkpoints.
    const uint32_t shifts_per_tile=cpu_backend?1u:2u;
    uint32_t dispatched_tiles=0;
    for(uint32_t shift=start_shift;shift<start_shift+640;shift+=64*shifts_per_tile){
        std::vector<U2> cpu_masks;
        if(cpu_backend){
            cpu_masks.resize(mask_count);
            fill_masks(std::span<U2,mask_count>(cpu_masks.data(),mask_count),step,shift);
        }else active_v3d->prepare_pair(step,shift);
        for(uint32_t base=0;base<10840;base+=SEEDS){
            uint32_t count=std::min(SEEDS,10840u-base);
            fill_seed_tile(seeds,n43,base,count);
            if(cpu_backend){
                auto n59=generate_cpu(seeds,K,count);
                auto records=sieve_cpu(n59,cpu_masks,s59);auto candidates=compact_cpu(records,shift);
                for(auto n:candidates){auto o=check_candidate(n,step);if(o.flags&2u)hits.push_back({o.count,o.first});}
            }else active_v3d->tile_pair(K,seeds,count,shift,hits);
            completed+=shifts_per_tile;
            tile_done(double(completed)/double(10*tiles_per_shift));
            if(max_tiles && ++dispatched_tiles>=max_tiles)throw std::runtime_error("diagnostic tile limit reached; no result committed");
        }
        if(cpu_backend)std::fprintf(stderr,"K=%u shift=%u AP10+ hits=%zu\n",K,shift,hits.size());
        else std::fprintf(stderr,"K=%u shifts=%u,%u AP10+ hits=%zu\n",K,shift,shift+64,hits.size());
    }
    std::sort(hits.begin(),hits.end(),[](const APHit& a,const APHit& b){
        return a.first<b.first || (a.first==b.first && a.length<b.length);
    });
    return hits;
}
