#include <vulkan/vulkan.h>
#include "ap27_v3d/small_primes.hpp"
#include "ap27_v3d/tile_layout.hpp"
#include "ap27_v3d/v3d_search.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

static constexpr uint64_t MOD=258559632607830ULL;
static constexpr uint64_t PRIM23=223092870ULL;
static constexpr uint32_t SEEDS=32, PER_SEED=12673;
static constexpr uint32_t N59_COUNT=SEEDS*PER_SEED;
static constexpr auto LAST_EARLY_PRIME=small_primes[ap27_v3d::FIRST_STAGE_PRIME_COUNT-1];
static constexpr uint32_t FIRST_MASK_COUNT=LAST_EARLY_PRIME.offset+LAST_EARLY_PRIME.p;
static_assert(MOD < ap27_v3d::SECOND_SHIFT_RECORD_TAG);
static constexpr uint64_t PRES5=126273308948010ULL;
static constexpr uint64_t PRES6=115526644356690ULL, PRES7=48784836341100ULL;
static constexpr uint64_t PRES8=100794433050510ULL;

void check(VkResult r,const char* what) { if(r!=VK_SUCCESS)throw std::runtime_error(std::string(what)+": VkResult "+std::to_string(r)); }
uint64_t pack(uint32_t lo,uint32_t hi) { return uint64_t(lo)|(uint64_t(hi)<<32); }
struct U2 { uint32_t lo,hi; };
U2 split(uint64_t x) { return {uint32_t(x),uint32_t(x>>32)}; }
struct U4 { uint32_t x,y,z,w; };
struct SeedData { U2 seeds[32]; U2 offsets[71]; };
uint64_t residue(uint64_t pres,uint32_t K) { return (pres*(K%17835)+((pres*17835)%MOD)*(K/17835))%MOD; }
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
void fill_masks(std::span<U2,mask_count> masks,uint64_t step,uint32_t shift){
    std::array<uint8_t,small_primes.back().p> ok{};
    for(const auto& prime:small_primes){
        uint32_t p=prime.p; std::fill_n(ok.begin(),p,uint8_t{1});
        for(uint32_t j=p-23;j<=p;j++) ok[(j*(step%p))%p]=0;
        uint32_t modp=MOD%p;
        for(uint32_t i=0;i<p;i++){
            uint64_t bits=0;
            uint32_t r=(i+shift*modp)%p;
            for(uint32_t b=0;b<64;b++){
                if(ok[r])bits|=1ULL<<b;
                r+=modp;
                if(r>=p)r-=p;
            }
            masks[prime.offset+i]=split(bits);
        }
    }
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
    std::array<Buffer,9> b{}; std::array<VkShaderModule,8> modules{};
    std::array<VkPipeline,8> pipelines{};
    // One in-flight tile: seeds/control change in mapped buffers; commands only
    // depend on K, shift and seed count (including the final partial tile).
    std::array<uint32_t,3> recorded_key{};
    std::array<U2,mask_count> base_masks{};
    uint64_t mask_step=0;
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
        constexpr std::array<VkDeviceSize,9> sizes={sizeof(SeedData),sizeof(U2)*N59_COUNT,
            sizeof(U2)*mask_count,sizeof(U4)*ap27_v3d::EARLY_RECORD_CAPACITY,
            sizeof(U2)*ap27_v3d::CANDIDATE_CAPACITY,
            sizeof(U4)*ap27_v3d::INTERMEDIATE_RECORD_CAPACITY,64,sizeof(U4)*FIRST_MASK_COUNT,
            sizeof(U4)*ap27_v3d::CANDIDATE_CAPACITY};
        for(size_t i=0;i<b.size();++i)
            b[i]=make_buffer(dev,gpu,sizes[i],i==6?VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT:0);
        VkDescriptorSetLayoutBinding lb[9]{};for(uint32_t i=0;i<b.size();i++){lb[i].binding=i;lb[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;lb[i].descriptorCount=1;lb[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
        VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};lc.bindingCount=b.size();lc.pBindings=lb;
        check(vkCreateDescriptorSetLayout(dev,&lc,nullptr,&dsl),"descriptor layout");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(ap27_v3d::PushConstants)};
        VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pc.setLayoutCount=1;pc.pSetLayouts=&dsl;pc.pushConstantRangeCount=1;pc.pPushConstantRanges=&range;
        check(vkCreatePipelineLayout(dev,&pc,nullptr,&layout),"pipeline layout");
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,9};VkDescriptorPoolCreateInfo dpc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dpc.maxSets=1;dpc.poolSizeCount=1;dpc.pPoolSizes=&ps;
        check(vkCreateDescriptorPool(dev,&dpc,nullptr,&pool),"descriptor pool");
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&dsl;
        check(vkAllocateDescriptorSets(dev,&da,&set),"descriptor set");
        VkDescriptorBufferInfo info[9]{};VkWriteDescriptorSet writes[9]{};
        for(uint32_t i=0;i<b.size();i++){info[i]={b[i].b,0,VK_WHOLE_SIZE};writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&info[i];}
        vkUpdateDescriptorSets(dev,b.size(),writes,0,nullptr);
        char path[4096];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
        if(n<0)throw std::runtime_error("cannot locate shader directory");path[n]=0;
        std::string dir(path);dir.resize(dir.find_last_of('/'));
        for(int i=0;i<8;i++){
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
        for(int i=0;i<8;i++){if(pipelines[i])vkDestroyPipeline(dev,pipelines[i],nullptr);if(modules[i])vkDestroyShaderModule(dev,modules[i],nullptr);}
        if(pool)vkDestroyDescriptorPool(dev,pool,nullptr);if(layout)vkDestroyPipelineLayout(dev,layout,nullptr);if(dsl)vkDestroyDescriptorSetLayout(dev,dsl,nullptr);
        for(auto& x:b)if(x.b)destroy(dev,x);
        if(dev)vkDestroyDevice(dev,nullptr);if(instance)vkDestroyInstance(instance,nullptr);
    }
    void prepare_pair(uint64_t step,uint32_t shift){
        auto masks=std::span<U2,mask_count>(static_cast<U2*>(b[2].ptr),mask_count);
        // For a fixed K, shift only rotates each prime's mask table.
        if(mask_step!=step) {
            fill_masks(base_masks,step,0);
            mask_step=step;
        }
        for(const auto& prime:small_primes) {
            uint32_t rotation=(uint64_t(shift)*(MOD%prime.p))%prime.p;
            const U2* base=base_masks.data()+prime.offset;
            std::copy_n(base+rotation,prime.p-rotation,masks.data()+prime.offset);
            std::copy_n(base,rotation,masks.data()+prime.offset+prime.p-rotation);
        }
        // The first 32 primes fetch both shifts in one aligned 16-byte lookup.
        auto* pairs=static_cast<U4*>(b[7].ptr);
        for(uint32_t pi=0;pi<ap27_v3d::FIRST_STAGE_PRIME_COUNT;pi++){
            auto prime=small_primes[pi];
            const uint32_t rotation=(64*MOD)%prime.p;
            for(uint32_t i=0;i<prime.p;i++){
                auto a=masks[prime.offset+i];
                auto other=masks[prime.offset+(i+rotation)%prime.p];
                pairs[prime.offset+i]={a.lo,a.hi,other.lo,other.hi};
            }
        }
    }
    void record_pair(uint32_t K,uint32_t count,uint32_t shift){
        uint64_t step=uint64_t(K)*PRIM23;
        uint64_t s59=residue(PRES8,K);uint32_t n59=count*PER_SEED;
        ap27_v3d::PushConstants push{uint32_t(s59),uint32_t(s59>>32),uint32_t(step),uint32_t(step>>32),
                                     shift,n59,ap27_v3d::CANDIDATE_CAPACITY,
                                     ap27_v3d::EARLY_RECORD_CAPACITY};
        check(vkResetCommandBuffer(cb,0),"reset command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};check(vkBeginCommandBuffer(cb,&begin),"begin command buffer");
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
        vkCmdPushConstants(cb,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);
        // Setup, early sieve, prepare/middle sieve, prepare/late sieve, PRP.
        for(uint32_t stage: {0u,1u,2u,6u,7u,3u,4u,5u}){
            vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[stage]);
            if(stage==0)vkCmdDispatch(cb,(n59+63)/64,1,1);
            else if(stage==1)vkCmdDispatch(cb,(n59+511)/512,1,1);
            else if(stage==2||stage==4||stage==7)vkCmdDispatch(cb,1,1,1);
            else vkCmdDispatchIndirect(cb,b[6].b,
                (stage==3||stage==6?ap27_v3d::SIEVE_DISPATCH_WORD:ap27_v3d::CHECK_DISPATCH_WORD)*sizeof(uint32_t));
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask=stage==5?VK_ACCESS_HOST_READ_BIT:(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
            VkPipelineStageFlags dst=stage==5?VK_PIPELINE_STAGE_HOST_BIT:(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,dst,0,1,&barrier,0,nullptr,0,nullptr);
        }
        check(vkEndCommandBuffer(cb),"end command buffer");
    }
    void tile_pair(uint32_t K,const SeedData& seeds,uint32_t count,uint32_t shift,std::vector<APHit>& hits){
        std::memcpy(b[0].ptr,&seeds,sizeof(seeds));
        std::memset(b[6].ptr,0,b[6].size);
        const std::array<uint32_t,3> key{K,count,shift};
        if(recorded_key!=key) {
            record_pair(K,count,shift);
            recorded_key=key;
        }
        check(vkResetFences(dev,1,&fence),"reset fence");
        VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};sub.commandBufferCount=1;sub.pCommandBuffers=&cb;
        check(vkQueueSubmit(queue,1,&sub,fence),"queue submit");
        check(vkWaitForFences(dev,1,&fence,VK_TRUE,300000000000ULL),"wait tile fence");
        const uint32_t* control=static_cast<const uint32_t*>(b[6].ptr);
        if(control[ap27_v3d::ERROR_FLAGS_WORD] ||
           control[ap27_v3d::EARLY_RECORD_COUNT_WORD]>ap27_v3d::EARLY_RECORD_CAPACITY ||
           control[ap27_v3d::INTERMEDIATE_RECORD_COUNT_WORD]>ap27_v3d::INTERMEDIATE_RECORD_CAPACITY ||
           control[ap27_v3d::CANDIDATE_COUNT_WORD]>ap27_v3d::CANDIDATE_CAPACITY ||
           control[ap27_v3d::FINAL_RECORD_COUNT_WORD]>
               control[ap27_v3d::INTERMEDIATE_RECORD_COUNT_WORD])
            throw std::runtime_error("V3D output overflow or invalid PRP arithmetic");
        // The PRP stage appends only AP10+ hits. Full per-candidate statuses
        // remain available for the oracle, but production never scans them.
        uint32_t hit_count=control[ap27_v3d::AP_HIT_COUNT_WORD];
        if(hit_count>control[ap27_v3d::CANDIDATE_COUNT_WORD])
            throw std::runtime_error("V3D hit count overflow");
        const U4* results=static_cast<const U4*>(b[8].ptr);
        for(uint32_t i=0;i<hit_count;i++) {
            if(!(results[i].w&2u) || results[i].x<10u)
                throw std::runtime_error("V3D invalid hit record");
            hits.push_back({results[i].x,pack(results[i].y,results[i].z)});
        }
    }
};

static std::unique_ptr<V3D> active_v3d;
void v3d_search_init(){
    active_v3d=std::make_unique<V3D>();
    std::fprintf(stderr,"AP27 backend: V3D Vulkan\n");
}
void v3d_search_cleanup(){active_v3d.reset();}
std::vector<APHit> search_ap27_k(unsigned K,unsigned start_shift,const uint64_t* n43,
                                const std::function<void(double)>& tile_done){
    if(!active_v3d)throw std::runtime_error("V3D not initialized");
    std::vector<APHit> hits;hits.reserve(256);
    uint64_t step=uint64_t(K)*PRIM23;
    SeedData seeds=make_seed_offsets(K);
    constexpr uint32_t tiles_per_shift=(10840+SEEDS-1)/SEEDS;
    uint32_t completed=0;
    // Restarts replay an interrupted K, so pairing does not change checkpoints.
    for(uint32_t shift=start_shift;shift<start_shift+640;shift+=128){
        active_v3d->prepare_pair(step,shift);
        for(uint32_t base=0;base<10840;base+=SEEDS){
            uint32_t count=std::min(SEEDS,10840u-base);
            fill_seed_tile(seeds,n43,base,count);
            active_v3d->tile_pair(K,seeds,count,shift,hits);
            completed+=2;
            tile_done(double(completed)/double(10*tiles_per_shift));
        }
        std::fprintf(stderr,"K=%u shifts=%u,%u AP10+ hits=%zu\n",K,shift,shift+64,hits.size());
    }
    std::sort(hits.begin(),hits.end(),[](const APHit& a,const APHit& b){
        return a.first<b.first || (a.first==b.first && a.length<b.length);
    });
    return hits;
}
