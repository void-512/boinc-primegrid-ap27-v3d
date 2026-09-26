#include <vulkan/vulkan.h>
#include "ap27_v3d/small_primes.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <time.h>

static constexpr uint64_t MOD=258559632607830ULL;
static constexpr uint64_t PRIM23=223092870ULL;
static constexpr uint32_t K=366384, SEEDS=32, PER_SEED=12673;
static constexpr uint32_t N59_COUNT=SEEDS*PER_SEED, CAP=1000000;
static constexpr uint64_t N0=106990415896110ULL, N30=94805198622871ULL;
static constexpr uint64_t S3=172373088405220ULL, S5=51711926521566ULL;
static constexpr uint64_t PRES2=16681266619860ULL, PRES3=181690552643340ULL;
static constexpr uint64_t PRES4=132432982555230ULL, PRES5=126273308948010ULL;
static constexpr uint64_t PRES6=115526644356690ULL, PRES7=48784836341100ULL;
static constexpr uint64_t PRES8=100794433050510ULL;
static constexpr uint32_t SMALL[]={7,11,13,17,19,23};

void check(VkResult r,const char* what) { if(r!=VK_SUCCESS){std::fprintf(stderr,"%s: VkResult %d\n",what,r);std::exit(2);} }
uint64_t process_ns() { timespec t{}; clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t); return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec; }
uint64_t pack(uint32_t lo,uint32_t hi) { return uint64_t(lo)|(uint64_t(hi)<<32); }
struct U2 { uint32_t lo,hi; };
U2 split(uint64_t x) { return {uint32_t(x),uint32_t(x>>32)}; }
struct U4 { uint32_t x,y,z,w; };
struct SeedData { U2 seeds[32]; U2 offsets[71]; };
struct Push { uint32_t s59lo,s59hi,steplo,stephi,shift,count,cap,reserved; };
struct SieveRec { uint64_t n, mask; bool operator<(const SieveRec& b) const {return std::tie(n,mask)<std::tie(b.n,b.mask);} bool operator==(const SieveRec& b) const {return n==b.n&&mask==b.mask;} };
struct Outcome { uint64_t n,first; uint32_t count,flags; bool operator<(const Outcome& b) const {return std::tie(n,count,first,flags)<std::tie(b.n,b.count,b.first,b.flags);} bool operator==(const Outcome& b) const {return n==b.n&&count==b.count&&first==b.first&&flags==b.flags;} };
uint64_t residue(uint64_t pres) { return (pres*(K%17835)+((pres*17835)%MOD)*(K/17835))%MOD; }
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
SeedData make_seeds(){
    SeedData d{}; uint64_t n0=(N0*(K%17835)+((N0*17835)%MOD)*(K/17835)+N30)%MOD;
    uint64_t s31=residue(PRES2),s37=residue(PRES3),s41=residue(PRES4);
    uint64_t s43=residue(PRES5),s47=residue(PRES6),s53=residue(PRES7);
    uint32_t count=0;
    for(int i31=0;i31<7;i31++)for(int i37=0;i37<13;i37++)
    if(i37-i31<=10 && i31-i37<=4)
    for(int i41=0;i41<17;i41++)
    if(i41-i31<=14 && i41-i37<=14 && i31-i41<=4 && i37-i41<=10)
    for(int i3=0;i3<2;i3++)for(int i5=0;i5<4;i5++){
        if(count<SEEDS) d.seeds[count]=split((n0+i3*S3+i5*S5+i31*s31+i37*s37+i41*s41)%MOD);
        count++;
    }
    if(count!=10840) throw std::runtime_error("n43 seed count mismatch");
    for(uint32_t i=0;i<19;i++)d.offsets[i]=split((i*s43)%MOD);
    for(uint32_t i=0;i<23;i++)d.offsets[19+i]=split((i*s47)%MOD);
    for(uint32_t i=0;i<29;i++)d.offsets[42+i]=split((i*s53)%MOD);
    return d;
}
std::vector<uint64_t> generate_cpu(const SeedData& seeds){
    std::vector<uint64_t> n(N59_COUNT);
    const uint64_t s43=residue(PRES5),s47=residue(PRES6),s53=residue(PRES7);
    uint32_t idx=0;
    for(uint32_t seed=0;seed<SEEDS;seed++){
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
    if(idx!=N59_COUNT)throw std::runtime_error("CPU setupn count mismatch");
    return n;
}
std::vector<U2> make_masks(uint64_t step,uint32_t shift){
    std::vector<U2> masks(mask_count);
    for(const auto& prime:small_primes){
        uint32_t p=prime.p; std::vector<uint8_t> ok(p,1);
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
    return masks;
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
    f.seekg(0);std::vector<uint32_t>b(size_t(n)/4);f.read(reinterpret_cast<char*>(b.data()),n);return b;
}
uint32_t memory_type(VkPhysicalDevice gpu,uint32_t bits,VkMemoryPropertyFlags flags){
    VkPhysicalDeviceMemoryProperties p;vkGetPhysicalDeviceMemoryProperties(gpu,&p);
    for(uint32_t i=0;i<p.memoryTypeCount;i++)if((bits&(1u<<i))&&(p.memoryTypes[i].propertyFlags&flags)==flags)return i;
    throw std::runtime_error("no host coherent V3D memory");
}
struct Buffer{VkBuffer b{};VkDeviceMemory m{};void* ptr{};VkDeviceSize size{};};
Buffer make_buffer(VkDevice dev,VkPhysicalDevice gpu,VkDeviceSize size,VkBufferUsageFlags usage){
    Buffer out;out.size=size;VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};c.size=size;c.usage=usage|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    check(vkCreateBuffer(dev,&c,nullptr,&out.b),"buffer create");
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(dev,out.b,&req);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;
    a.memoryTypeIndex=memory_type(gpu,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(dev,&a,nullptr,&out.m),"buffer memory");
    check(vkBindBufferMemory(dev,out.b,out.m,0),"buffer bind");
    check(vkMapMemory(dev,out.m,0,size,0,&out.ptr),"buffer map");return out;
}
void destroy(VkDevice dev,Buffer& b){vkUnmapMemory(dev,b.m);vkDestroyBuffer(dev,b.b,nullptr);vkFreeMemory(dev,b.m,nullptr);}

int main(int argc,char** argv){
try {
    uint32_t shifts=argc>1 ? uint32_t(std::strtoul(argv[1],nullptr,10)) : 1;
    if(shifts!=1&&shifts!=10)throw std::runtime_error("usage: tile_run [1|10]");
    auto wall0=std::chrono::steady_clock::now();uint64_t cpu0=process_ns();
    const uint64_t step=uint64_t(K)*PRIM23,s59=residue(PRES8);
    SeedData seeds=make_seeds(); auto expected_n59=generate_cpu(seeds);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_2;app.pApplicationName="AP27 exact tile";
    VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ic.pApplicationInfo=&app;
    VkInstance instance;check(vkCreateInstance(&ic,nullptr,&instance),"instance");
    uint32_t ng=0;check(vkEnumeratePhysicalDevices(instance,&ng,nullptr),"enumerate");
    std::vector<VkPhysicalDevice> gpus(ng);check(vkEnumeratePhysicalDevices(instance,&ng,gpus.data()),"devices");
    VkPhysicalDevice gpu{};VkPhysicalDeviceProperties props{};
    for(auto g:gpus){VkPhysicalDeviceProperties p;vkGetPhysicalDeviceProperties(g,&p);if(p.vendorID==0x14e4&&std::strstr(p.deviceName,"V3D")){gpu=g;props=p;break;}}
    if(!gpu)throw std::runtime_error("V3D unavailable; refusing llvmpipe");
    uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,nullptr);
    std::vector<VkQueueFamilyProperties> qprops(nq);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,qprops.data());
    uint32_t family=nq;for(uint32_t i=0;i<nq;i++)if(qprops[i].queueFlags&VK_QUEUE_COMPUTE_BIT){family=i;break;}
    if(family==nq||!qprops[family].timestampValidBits)throw std::runtime_error("V3D compute/timestamps unavailable");
    float priority=1;VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qc.queueFamilyIndex=family;qc.queueCount=1;qc.pQueuePriorities=&priority;
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR feature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};feature.pipelineExecutableInfo=VK_TRUE;
    const char* extension=VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME;
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.pNext=&feature;dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qc;dc.enabledExtensionCount=1;dc.ppEnabledExtensionNames=&extension;
    VkDevice dev;check(vkCreateDevice(gpu,&dc,nullptr,&dev),"V3D device");VkQueue queue;vkGetDeviceQueue(dev,family,0,&queue);
    std::array<Buffer,7> b={
        make_buffer(dev,gpu,sizeof(SeedData),0),
        make_buffer(dev,gpu,sizeof(U2)*N59_COUNT,0),
        make_buffer(dev,gpu,sizeof(U2)*mask_count,0),
        make_buffer(dev,gpu,sizeof(U4)*CAP,0),
        make_buffer(dev,gpu,sizeof(U2)*CAP,0),
        make_buffer(dev,gpu,sizeof(U4)*CAP,0),
        make_buffer(dev,gpu,64,VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)};
    std::memcpy(b[0].ptr,&seeds,sizeof(seeds));
    VkDescriptorSetLayoutBinding lb[7]{};for(uint32_t i=0;i<7;i++){lb[i].binding=i;lb[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;lb[i].descriptorCount=1;lb[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
    VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};lc.bindingCount=7;lc.pBindings=lb;
    VkDescriptorSetLayout dsl;check(vkCreateDescriptorSetLayout(dev,&lc,nullptr,&dsl),"descriptor layout");
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};
    VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pc.setLayoutCount=1;pc.pSetLayouts=&dsl;pc.pushConstantRangeCount=1;pc.pPushConstantRanges=&range;
    VkPipelineLayout layout;check(vkCreatePipelineLayout(dev,&pc,nullptr,&layout),"pipeline layout");
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,7};VkDescriptorPoolCreateInfo dpc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dpc.maxSets=1;dpc.poolSizeCount=1;dpc.pPoolSizes=&ps;
    VkDescriptorPool pool;check(vkCreateDescriptorPool(dev,&dpc,nullptr,&pool),"descriptor pool");
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&dsl;
    VkDescriptorSet set;check(vkAllocateDescriptorSets(dev,&da,&set),"descriptor set");
    VkDescriptorBufferInfo info[7]{};VkWriteDescriptorSet writes[7]{};
    for(uint32_t i=0;i<7;i++){info[i]={b[i].b,0,VK_WHOLE_SIZE};writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&info[i];}
    vkUpdateDescriptorSets(dev,7,writes,0,nullptr);
    std::array<VkShaderModule,6> modules{};std::array<VkPipeline,6> pipelines{};
    for(int i=0;i<6;i++){
        const char* shader_dir=std::getenv("AP27_SHADER_DIR");
        auto words=read_spirv(std::string(shader_dir?shader_dir:"build/bin")+
                              "/stage_"+std::to_string(i)+".spv");
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=words.size()*4;sm.pCode=words.data();
        check(vkCreateShaderModule(dev,&sm,nullptr,&modules[i]),"shader module");
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.layout=layout;
        cp.flags=VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
        cp.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=modules[i];cp.stage.pName="main";
        check(vkCreateComputePipelines(dev,VK_NULL_HANDLE,1,&cp,nullptr,&pipelines[i]),"compute pipeline");
    }
    auto gp=reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(vkGetDeviceProcAddr(dev,"vkGetPipelineExecutablePropertiesKHR"));
    auto gs=reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(vkGetDeviceProcAddr(dev,"vkGetPipelineExecutableStatisticsKHR"));
    for(int i:{0,1,3,5})if(gp&&gs){
        VkPipelineInfoKHR pi{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};pi.pipeline=pipelines[i];uint32_t ne=0;check(gp(dev,&pi,&ne,nullptr),"exec count");
        if(!ne)continue;
        VkPipelineExecutableInfoKHR ei{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};ei.pipeline=pipelines[i];
        uint32_t ns=0;check(gs(dev,&ei,&ns,nullptr),"stat count");std::vector<VkPipelineExecutableStatisticKHR> stats(ns);
        for(auto& s:stats)s.sType=VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
        check(gs(dev,&ei,&ns,stats.data()),"statistics");
        std::printf("shader_stage=%d",i);for(auto& s:stats)if(std::strcmp(s.name,"Instruction Count")==0||std::strcmp(s.name,"Spill Size")==0||std::strcmp(s.name,"Thread Count")==0)std::printf(" %s=%llu",s.name,(unsigned long long)s.value.u64);std::printf("\n");
    }
    VkQueryPoolCreateInfo qpc{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qpc.queryType=VK_QUERY_TYPE_TIMESTAMP;qpc.queryCount=12;
    VkQueryPool qp;check(vkCreateQueryPool(dev,&qpc,nullptr,&qp),"timestamp pool");
    VkCommandPoolCreateInfo cpc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpc.queueFamilyIndex=family;cpc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool command_pool;check(vkCreateCommandPool(dev,&cpc,nullptr,&command_pool),"command pool");
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=command_pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
    VkCommandBuffer cb;check(vkAllocateCommandBuffers(dev,&ca,&cb),"command buffer");
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;check(vkCreateFence(dev,&fc,nullptr,&fence),"fence");
    std::printf("device=%s driver=%u.%u.%u n59=%u capacity=%u shifts=%u\n",props.deviceName,VK_VERSION_MAJOR(props.driverVersion),VK_VERSION_MINOR(props.driverVersion),VK_VERSION_PATCH(props.driverVersion),N59_COUNT,CAP,shifts);
    double total_gpu_ms=0,total_gpu_window_cpu_ms=0,total_gpu_window_wall_ms=0,total_ref_ms=0;
    for(uint32_t shift_i=0;shift_i<shifts;shift_i++){
        uint32_t shift=64*shift_i;
        auto ref_start=std::chrono::steady_clock::now();
        auto masks=make_masks(step,shift);std::memcpy(b[2].ptr,masks.data(),b[2].size);
        auto sieve_ref=sieve_cpu(expected_n59,masks,s59);
        auto cand_ref=compact_cpu(sieve_ref,shift);
        std::vector<Outcome> outcome_ref;outcome_ref.reserve(cand_ref.size());
        for(uint64_t n:cand_ref)outcome_ref.push_back(check_candidate(n,step));
        total_ref_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-ref_start).count();
        std::memset(b[6].ptr,0,b[6].size);
        Push push{uint32_t(s59),uint32_t(s59>>32),uint32_t(step),uint32_t(step>>32),shift,N59_COUNT,CAP,0};
        check(vkResetCommandBuffer(cb,0),"reset command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};check(vkBeginCommandBuffer(cb,&begin),"begin command buffer");
        vkCmdResetQueryPool(cb,qp,0,12);
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
        vkCmdPushConstants(cb,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);
        for(uint32_t stage=0;stage<6;stage++){
            vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,qp,2*stage);
            vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[stage]);
            if(stage==0)vkCmdDispatch(cb,(N59_COUNT+63)/64,1,1);
            else if(stage==1)vkCmdDispatch(cb,(N59_COUNT+511)/512,1,1);
            else if(stage==2||stage==4)vkCmdDispatch(cb,1,1,1);
            else vkCmdDispatchIndirect(cb,b[6].b,(stage==3?4:8)*sizeof(uint32_t));
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            if(stage==5)barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            VkPipelineStageFlags dst=stage==5?VK_PIPELINE_STAGE_HOST_BIT:(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,dst,0,1,&barrier,0,nullptr,0,nullptr);
            vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,qp,2*stage+1);
        }
        check(vkEndCommandBuffer(cb),"end command buffer");
        check(vkResetFences(dev,1,&fence),"reset fence");
        VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};sub.commandBufferCount=1;sub.pCommandBuffers=&cb;
        auto gwall=std::chrono::steady_clock::now();uint64_t gcpu=process_ns();
        check(vkQueueSubmit(queue,1,&sub,fence),"queue submit");
        check(vkWaitForFences(dev,1,&fence,VK_TRUE,300000000000ULL),"wait tile fence");
        double window_cpu_ms=double(process_ns()-gcpu)/1e6;
        double window_wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-gwall).count();
        total_gpu_window_cpu_ms+=window_cpu_ms;total_gpu_window_wall_ms+=window_wall_ms;
        uint64_t ticks[12]{};check(vkGetQueryPoolResults(dev,qp,0,12,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"get timestamps");
        double gpu_stage[6]{};double gpu_ms=0;
        for(int s=0;s<6;s++){gpu_stage[s]=double(ticks[2*s+1]-ticks[2*s])*props.limits.timestampPeriod/1e6;gpu_ms+=gpu_stage[s];}
        total_gpu_ms+=gpu_ms;
        const uint32_t* control=static_cast<const uint32_t*>(b[6].ptr);
        uint32_t nr=control[0],nc=control[1],overflow=control[2],hits=control[3];
        std::printf("shift=%u gpu_ms setup=%.3f sieve=%.3f prep_compact=%.3f compact=%.3f prep_check=%.3f check=%.3f sum=%.3f wall_ms=%.3f process_cpu_ms=%.3f records=%u candidates=%u hits=%u overflow=%u ref_records=%zu ref_candidates=%zu\n",
                    shift,gpu_stage[0],gpu_stage[1],gpu_stage[2],gpu_stage[3],gpu_stage[4],gpu_stage[5],gpu_ms,window_wall_ms,window_cpu_ms,nr,nc,hits,overflow,sieve_ref.size(),cand_ref.size());
        if(overflow)throw std::runtime_error("bounded output overflow; retry with larger capacity or smaller tile");
        const U2* actual_n59=static_cast<const U2*>(b[1].ptr);
        for(uint32_t i=0;i<N59_COUNT;i++)if(pack(actual_n59[i].lo,actual_n59[i].hi)!=expected_n59[i]){
            std::fprintf(stderr,"n59 mismatch shift=%u idx=%u expected=%llu actual=%llu\n",shift,i,(unsigned long long)expected_n59[i],(unsigned long long)pack(actual_n59[i].lo,actual_n59[i].hi));
            throw std::runtime_error("GPU setupn differs from CPU");
        }
        if(nr!=sieve_ref.size())throw std::runtime_error("sieve record count mismatch");
        const U4* sr=static_cast<const U4*>(b[3].ptr);std::vector<SieveRec> sieve_gpu; sieve_gpu.reserve(nr);
        for(uint32_t i=0;i<nr;i++)sieve_gpu.push_back({pack(sr[i].x,sr[i].y),pack(sr[i].z,sr[i].w)});
        std::sort(sieve_gpu.begin(),sieve_gpu.end());std::sort(sieve_ref.begin(),sieve_ref.end());
        if(sieve_gpu!=sieve_ref){
            auto mismatch=std::mismatch(sieve_gpu.begin(),sieve_gpu.end(),sieve_ref.begin());
            size_t i=size_t(mismatch.first-sieve_gpu.begin());
            std::fprintf(stderr,"sieve mismatch shift=%u sorted_index=%zu GPU=(%llu,%llx) CPU=(%llu,%llx)\n",shift,i,(unsigned long long)sieve_gpu[i].n,(unsigned long long)sieve_gpu[i].mask,(unsigned long long)sieve_ref[i].n,(unsigned long long)sieve_ref[i].mask);
            throw std::runtime_error("GPU sieve differs from CPU");
        }
        if(nc!=cand_ref.size())throw std::runtime_error("candidate count mismatch");
        const U2* cn=static_cast<const U2*>(b[4].ptr);std::vector<uint64_t> cand_gpu;cand_gpu.reserve(nc);
        for(uint32_t i=0;i<nc;i++)cand_gpu.push_back(pack(cn[i].lo,cn[i].hi));
        std::sort(cand_gpu.begin(),cand_gpu.end());std::sort(cand_ref.begin(),cand_ref.end());
        if(cand_gpu!=cand_ref){
            auto mismatch=std::mismatch(cand_gpu.begin(),cand_gpu.end(),cand_ref.begin());size_t i=size_t(mismatch.first-cand_gpu.begin());
            std::fprintf(stderr,"candidate mismatch shift=%u sorted_index=%zu GPU=%llu CPU=%llu\n",shift,i,(unsigned long long)cand_gpu[i],(unsigned long long)cand_ref[i]);
            throw std::runtime_error("GPU candidates differ from CPU");
        }
        const U4* st=static_cast<const U4*>(b[5].ptr);std::vector<Outcome> outcome_gpu;outcome_gpu.reserve(nc);
        for(uint32_t i=0;i<nc;i++)outcome_gpu.push_back({pack(cn[i].lo,cn[i].hi),pack(st[i].y,st[i].z),st[i].x,st[i].w});
        std::sort(outcome_gpu.begin(),outcome_gpu.end());std::sort(outcome_ref.begin(),outcome_ref.end());
        if(outcome_gpu!=outcome_ref){
            auto mismatch=std::mismatch(outcome_gpu.begin(),outcome_gpu.end(),outcome_ref.begin());size_t i=size_t(mismatch.first-outcome_gpu.begin());
            std::fprintf(stderr,"PRP mismatch shift=%u sorted_index=%zu GPU=(%llu,%u,%llu,%u) CPU=(%llu,%u,%llu,%u)\n",shift,i,(unsigned long long)outcome_gpu[i].n,outcome_gpu[i].count,(unsigned long long)outcome_gpu[i].first,outcome_gpu[i].flags,(unsigned long long)outcome_ref[i].n,outcome_ref[i].count,(unsigned long long)outcome_ref[i].first,outcome_ref[i].flags);
            throw std::runtime_error("GPU checkn differs from CPU");
        }
        uint32_t expected_hits=0;for(const auto& o:outcome_ref)if(o.flags&2)expected_hits++;
        if(hits!=expected_hits)throw std::runtime_error("AP hit count mismatch");
        for(const auto& o:outcome_ref)if(o.flags&2)
            std::printf("AP_HIT shift=%u length=%u first=%llu candidate=%llu\n",shift,o.count,(unsigned long long)o.first,(unsigned long long)o.n);
        std::printf("shift=%u PASS n59=%u sieve_records=%u compacted=%u PRP=%u AP_hits=%u\n",shift,N59_COUNT,nr,nc,nc,hits);
        std::fflush(stdout);
    }
    auto wall1=std::chrono::steady_clock::now();double total_wall_ms=std::chrono::duration<double,std::milli>(wall1-wall0).count();double total_cpu_ms=double(process_ns()-cpu0)/1e6;
    std::printf("TOTAL shifts=%u gpu_stage_sum_ms=%.3f submit_wait_wall_ms=%.3f submit_wait_process_cpu_ms=%.3f cpu_reference_ms=%.3f process_wall_ms=%.3f process_cpu_ms=%.3f submissions=%u fence_waits=%u mapped_readbacks=%u\n",shifts,total_gpu_ms,total_gpu_window_wall_ms,total_gpu_window_cpu_ms,total_ref_ms,total_wall_ms,total_cpu_ms,shifts,shifts,5*shifts);
    vkDestroyFence(dev,fence,nullptr);vkDestroyCommandPool(dev,command_pool,nullptr);vkDestroyQueryPool(dev,qp,nullptr);
    for(int i=0;i<6;i++){vkDestroyPipeline(dev,pipelines[i],nullptr);vkDestroyShaderModule(dev,modules[i],nullptr);}
    vkDestroyDescriptorPool(dev,pool,nullptr);vkDestroyPipelineLayout(dev,layout,nullptr);vkDestroyDescriptorSetLayout(dev,dsl,nullptr);
    for(auto& x:b)destroy(dev,x);
    vkDestroyDevice(dev,nullptr);vkDestroyInstance(instance,nullptr);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"ERROR: %s\n",e.what());return 2;}
}
