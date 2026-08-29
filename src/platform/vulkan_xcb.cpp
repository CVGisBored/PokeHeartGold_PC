#include "platform/vulkan_xcb.hpp"
#include "platform/vk_min.h"
#include "game/game.hpp"
#include "game/input.hpp"
#include "game/render.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <iostream>
#include <linux/joystick.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

static void vkcheck(VkResult r,const char* what){ if(r!=VK_SUCCESS && r!=VK_SUBOPTIMAL_KHR) throw std::runtime_error(std::string(what)+" failed: "+std::to_string(r)); }
template<class T> static T load_i(PFN_vkGetInstanceProcAddr g,VkInstance i,const char* n){ auto p=reinterpret_cast<T>(g(i,n)); if(!p) throw std::runtime_error(std::string("missing Vulkan function ")+n); return p; }
template<class T> static T load_d(PFN_vkGetDeviceProcAddr g,VkDevice d,const char* n){ auto p=reinterpret_cast<T>(g(d,n)); if(!p) throw std::runtime_error(std::string("missing Vulkan device function ")+n); return p; }

namespace {
constexpr VkPresentModeKHR kForcedVsyncPresentMode=VK_PRESENT_MODE_FIFO_KHR; // guaranteed VBlank/VSync mode
using Glyph = std::array<uint8_t,7>;
const Glyph& glyphFor(char in){
    static const Glyph blank{0,0,0,0,0,0,0};
    static const std::unordered_map<char,Glyph> g={
        {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},
        {'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},{'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},
        {'I',{31,4,4,4,4,4,31}},{'J',{7,2,2,2,18,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
        {'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},{'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
        {'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},{'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},
        {'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
        {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
        {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},
        {'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},{'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},
        {'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,1,14}},
        {'-',{0,0,0,31,0,0,0}},{'_',{0,0,0,0,0,0,31}},{'.',{0,0,0,0,0,12,12}},{',',{0,0,0,0,4,4,8}},
        {':',{0,12,12,0,12,12,0}},{';',{0,12,12,0,4,4,8}},{'!',{4,4,4,4,4,0,4}},{'?',{14,17,1,2,4,0,4}},
        {'/',{1,2,2,4,8,8,16}},{'\\',{16,8,8,4,2,2,1}},{'\'',{4,4,8,0,0,0,0}},{'"',{10,10,0,0,0,0,0}},
        {'>',{16,8,4,2,4,8,16}},{'<',{1,2,4,8,4,2,1}},{'=',{0,31,0,31,0,0,0}},{'+',{0,4,4,31,4,4,0}},
        {'(',{2,4,8,8,8,4,2}},{')',{8,4,2,2,2,4,8}},{'[',{14,8,8,8,8,8,14}},{']',{14,2,2,2,2,2,14}},
        {'#',{10,31,10,10,31,10,0}},{'*',{0,10,4,31,4,10,0}},{'%',{17,2,4,8,16,17,0}},{'@',{14,17,23,21,23,16,14}}
    };
    char c=in; if(c>='a'&&c<='z')c=char(c-'a'+'A'); auto it=g.find(c); return it==g.end()?blank:it->second;
}
}

struct VulkanXcbRenderer::Impl {
    uint32_t width=1280,height=720;
    xcb_connection_t* xc=nullptr; xcb_screen_t* screen=nullptr; xcb_window_t win=0; xcb_atom_t wmDelete=0; bool resizePending=false;
    void* vkLib=nullptr; PFN_vkGetInstanceProcAddr gip=nullptr; PFN_vkGetDeviceProcAddr gdp=nullptr;
    VkInstance instance=nullptr; VkSurfaceKHR surface=0; VkPhysicalDevice phys=nullptr; VkDevice dev=nullptr; VkQueue queue=nullptr; uint32_t qfam=0;
    VkSwapchainKHR swap=0; VkFormat format=VK_FORMAT_B8G8R8A8_UNORM; VkExtent2D extent{};
    std::vector<VkImage> images; std::vector<VkImageView> views; std::vector<VkFramebuffer> fbs; std::vector<VkCommandBuffer> cmds;
    VkRenderPass renderPass=0; VkCommandPool pool=0; VkSemaphore imageAvail=0,renderDone=0;
    VkBuffer stagingBuffer=0; VkDeviceMemory stagingMemory=0; void* stagingMapped=nullptr; VkDeviceSize stagingSize=0;
    InputState input{}; std::array<bool,static_cast<std::size_t>(GameButton::Count)> keyboardDown{},joyDown{};
    int js=-1; int16_t joyX=0,joyY=0,joyCenterX=0,joyCenterY=0; bool joystickArmed=false;

    PFN_vkDestroyInstance DestroyInstance{}; PFN_vkCreateXcbSurfaceKHR CreateSurface{}; PFN_vkDestroySurfaceKHR DestroySurface{};
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices{}; PFN_vkGetPhysicalDeviceQueueFamilyProperties GetQProps{}; PFN_vkGetPhysicalDeviceMemoryProperties GetMemoryProps{};
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetSurfaceSupport{}; PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetCaps{};
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetFormats{}; PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPresentModes{};
    PFN_vkCreateDevice CreateDevice{}; PFN_vkDestroyDevice DestroyDevice{}; PFN_vkGetDeviceQueue GetDeviceQueue{};
    PFN_vkCreateSwapchainKHR CreateSwapchain{}; PFN_vkDestroySwapchainKHR DestroySwapchain{}; PFN_vkGetSwapchainImagesKHR GetSwapImages{};
    PFN_vkCreateImageView CreateImageView{}; PFN_vkDestroyImageView DestroyImageView{}; PFN_vkCreateRenderPass CreateRenderPass{}; PFN_vkDestroyRenderPass DestroyRenderPass{};
    PFN_vkCreateFramebuffer CreateFramebuffer{}; PFN_vkDestroyFramebuffer DestroyFramebuffer{}; PFN_vkCreateCommandPool CreateCommandPool{}; PFN_vkDestroyCommandPool DestroyCommandPool{};
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers{}; PFN_vkResetCommandBuffer ResetCommandBuffer{}; PFN_vkBeginCommandBuffer BeginCommandBuffer{}; PFN_vkEndCommandBuffer EndCommandBuffer{};
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass{}; PFN_vkCmdEndRenderPass CmdEndRenderPass{}; PFN_vkCmdClearAttachments CmdClearAttachments{};
    PFN_vkCreateSemaphore CreateSemaphore{}; PFN_vkDestroySemaphore DestroySemaphore{}; PFN_vkAcquireNextImageKHR Acquire{}; PFN_vkQueueSubmit QueueSubmit{};
    PFN_vkQueuePresentKHR QueuePresent{}; PFN_vkQueueWaitIdle QueueWaitIdle{}; PFN_vkDeviceWaitIdle DeviceWaitIdle{};
    PFN_vkCreateBuffer CreateBuffer{}; PFN_vkDestroyBuffer DestroyBuffer{}; PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements{};
    PFN_vkAllocateMemory AllocateMemory{}; PFN_vkFreeMemory FreeMemory{}; PFN_vkBindBufferMemory BindBufferMemory{};
    PFN_vkMapMemory MapMemory{}; PFN_vkUnmapMemory UnmapMemory{}; PFN_vkCmdPipelineBarrier CmdPipelineBarrier{}; PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage{};

    ~Impl(){cleanup();}
    xcb_atom_t atom(const char* name){auto c=xcb_intern_atom(xc,0,std::strlen(name),name);auto* r=xcb_intern_atom_reply(xc,c,nullptr);if(!r)return 0;auto a=r->atom;free(r);return a;}
    void makeWindow(const std::string& title){
        int sn=0;xc=xcb_connect(nullptr,&sn);if(!xc||xcb_connection_has_error(xc))throw std::runtime_error("cannot connect to X server");const xcb_setup_t* setup=xcb_get_setup(xc);auto it=xcb_setup_roots_iterator(setup);for(int i=0;i<sn;i++)xcb_screen_next(&it);screen=it.data;
        win=xcb_generate_id(xc);uint32_t mask=XCB_CW_BACK_PIXEL|XCB_CW_EVENT_MASK;uint32_t vals[]={screen->black_pixel,XCB_EVENT_MASK_EXPOSURE|XCB_EVENT_MASK_KEY_PRESS|XCB_EVENT_MASK_KEY_RELEASE|XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_POINTER_MOTION|XCB_EVENT_MASK_STRUCTURE_NOTIFY|XCB_EVENT_MASK_FOCUS_CHANGE};
        xcb_create_window(xc,XCB_COPY_FROM_PARENT,win,screen->root,80,80,width,height,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,screen->root_visual,mask,vals);xcb_change_property(xc,XCB_PROP_MODE_REPLACE,win,XCB_ATOM_WM_NAME,XCB_ATOM_STRING,8,title.size(),title.c_str());auto protocols=atom("WM_PROTOCOLS");wmDelete=atom("WM_DELETE_WINDOW");if(protocols&&wmDelete)xcb_change_property(xc,XCB_PROP_MODE_REPLACE,win,protocols,XCB_ATOM_ATOM,32,1,&wmDelete);xcb_map_window(xc,win);xcb_flush(xc);
        if(const char* noController=std::getenv("HG_DISABLE_CONTROLLER"); noController&&std::string(noController)!="0") {
            js=-1; std::cout<<"Controller: disabled by --no-controller / HG_DISABLE_CONTROLLER\n";
        } else {
            js=open("/dev/input/js0",O_RDONLY|O_NONBLOCK);
            if(js>=0)std::cout<<"Controller: /dev/input/js0 detected (movement axes locked until a controller button is pressed)\n";
            else std::cout<<"Controller: none (keyboard enabled)\n";
        }
    }
    void loadInstanceFns(){DestroyInstance=load_i<PFN_vkDestroyInstance>(gip,instance,"vkDestroyInstance");CreateSurface=load_i<PFN_vkCreateXcbSurfaceKHR>(gip,instance,"vkCreateXcbSurfaceKHR");DestroySurface=load_i<PFN_vkDestroySurfaceKHR>(gip,instance,"vkDestroySurfaceKHR");EnumeratePhysicalDevices=load_i<PFN_vkEnumeratePhysicalDevices>(gip,instance,"vkEnumeratePhysicalDevices");GetQProps=load_i<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gip,instance,"vkGetPhysicalDeviceQueueFamilyProperties");GetMemoryProps=load_i<PFN_vkGetPhysicalDeviceMemoryProperties>(gip,instance,"vkGetPhysicalDeviceMemoryProperties");GetSurfaceSupport=load_i<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(gip,instance,"vkGetPhysicalDeviceSurfaceSupportKHR");GetCaps=load_i<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(gip,instance,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");GetFormats=load_i<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(gip,instance,"vkGetPhysicalDeviceSurfaceFormatsKHR");GetPresentModes=load_i<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(gip,instance,"vkGetPhysicalDeviceSurfacePresentModesKHR");CreateDevice=load_i<PFN_vkCreateDevice>(gip,instance,"vkCreateDevice");gdp=load_i<PFN_vkGetDeviceProcAddr>(gip,instance,"vkGetDeviceProcAddr");}
    void loadDeviceFns(){
#define LD(name) name=load_d<PFN_vk##name>(gdp,dev,"vk" #name)
        LD(DestroyDevice);LD(GetDeviceQueue);LD(CreateBuffer);LD(DestroyBuffer);LD(GetBufferMemoryRequirements);LD(AllocateMemory);LD(FreeMemory);LD(BindBufferMemory);LD(MapMemory);LD(UnmapMemory);LD(CmdPipelineBarrier);LD(CmdCopyBufferToImage);LD(CreateImageView);LD(DestroyImageView);LD(CreateRenderPass);LD(DestroyRenderPass);LD(CreateFramebuffer);LD(DestroyFramebuffer);LD(CreateCommandPool);LD(DestroyCommandPool);LD(AllocateCommandBuffers);LD(ResetCommandBuffer);LD(BeginCommandBuffer);LD(EndCommandBuffer);LD(CmdBeginRenderPass);LD(CmdEndRenderPass);LD(CmdClearAttachments);LD(CreateSemaphore);LD(DestroySemaphore);LD(QueueSubmit);LD(QueueWaitIdle);LD(DeviceWaitIdle);
#undef LD
        CreateSwapchain=load_d<PFN_vkCreateSwapchainKHR>(gdp,dev,"vkCreateSwapchainKHR");DestroySwapchain=load_d<PFN_vkDestroySwapchainKHR>(gdp,dev,"vkDestroySwapchainKHR");GetSwapImages=load_d<PFN_vkGetSwapchainImagesKHR>(gdp,dev,"vkGetSwapchainImagesKHR");Acquire=load_d<PFN_vkAcquireNextImageKHR>(gdp,dev,"vkAcquireNextImageKHR");QueuePresent=load_d<PFN_vkQueuePresentKHR>(gdp,dev,"vkQueuePresentKHR");
    }
    void initVulkan(){
        vkLib=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);if(!vkLib)throw std::runtime_error("libvulkan.so.1 not found");gip=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(vkLib,"vkGetInstanceProcAddr"));if(!gip)throw std::runtime_error("vkGetInstanceProcAddr missing");auto CreateInstance=reinterpret_cast<PFN_vkCreateInstance>(gip(nullptr,"vkCreateInstance"));if(!CreateInstance)throw std::runtime_error("vkCreateInstance missing");
        VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO,nullptr,"HeartGold Native Port Opening & New Game",9,"HG Native",9,VK_API_VERSION_1_0};const char* exts[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_XCB_SURFACE_EXTENSION_NAME};VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,nullptr,0,&ai,0,nullptr,2,exts};vkcheck(CreateInstance(&ci,nullptr,&instance),"vkCreateInstance");loadInstanceFns();VkXcbSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,nullptr,0,xc,win};vkcheck(CreateSurface(instance,&sci,nullptr,&surface),"vkCreateXcbSurfaceKHR");
        uint32_t pc=0;vkcheck(EnumeratePhysicalDevices(instance,&pc,nullptr),"enumerate GPUs");if(!pc)throw std::runtime_error("no Vulkan physical device");std::vector<VkPhysicalDevice> ps(pc);EnumeratePhysicalDevices(instance,&pc,ps.data());bool found=false;for(auto pdev:ps){uint32_t qc=0;GetQProps(pdev,&qc,nullptr);std::vector<VkQueueFamilyProperties> qp(qc);GetQProps(pdev,&qc,qp.data());for(uint32_t i=0;i<qc;i++){VkBool32 present=0;GetSurfaceSupport(pdev,i,surface,&present);if((qp[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&present){phys=pdev;qfam=i;found=true;break;}}if(found)break;}if(!found)throw std::runtime_error("no graphics+present Vulkan queue");
        float priority=1.0f;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,nullptr,0,qfam,1,&priority};const char* dext[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,nullptr,0,1,&qci,0,nullptr,1,dext,nullptr};vkcheck(CreateDevice(phys,&dci,nullptr,&dev),"vkCreateDevice");loadDeviceFns();GetDeviceQueue(dev,qfam,0,&queue);VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,nullptr,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,qfam};vkcheck(CreateCommandPool(dev,&pci,nullptr,&pool),"vkCreateCommandPool");VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,nullptr,0};vkcheck(CreateSemaphore(dev,&sem,nullptr,&imageAvail),"image semaphore");vkcheck(CreateSemaphore(dev,&sem,nullptr,&renderDone),"render semaphore");createSwapchain();std::cout<<"VSync: ON (Vulkan FIFO, forced)\n";
    }
    void destroySwapchain(){if(!dev)return;if(DeviceWaitIdle)DeviceWaitIdle(dev);for(auto f:fbs)DestroyFramebuffer(dev,f,nullptr);fbs.clear();if(renderPass){DestroyRenderPass(dev,renderPass,nullptr);renderPass=0;}for(auto v:views)DestroyImageView(dev,v,nullptr);views.clear();images.clear();cmds.clear();if(swap){DestroySwapchain(dev,swap,nullptr);swap=0;}}
    void createSwapchain(){
        VkSurfaceCapabilitiesKHR caps{};vkcheck(GetCaps(phys,surface,&caps),"surface caps");if((caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT)==0)throw std::runtime_error("Vulkan swapchain does not support transfer-destination images required by the full-resolution renderer");uint32_t fc=0;GetFormats(phys,surface,&fc,nullptr);if(!fc)throw std::runtime_error("no surface formats");std::vector<VkSurfaceFormatKHR> fs(fc);GetFormats(phys,surface,&fc,fs.data());auto chosen=fs[0];for(auto f:fs)if(f.format==VK_FORMAT_B8G8R8A8_UNORM||f.format==VK_FORMAT_B8G8R8A8_SRGB){chosen=f;break;}format=chosen.format;
        if(caps.currentExtent.width!=0xFFFFFFFFu)extent=caps.currentExtent;else{extent.width=std::clamp(width,caps.minImageExtent.width,caps.maxImageExtent.width);extent.height=std::clamp(height,caps.minImageExtent.height,caps.maxImageExtent.height);}width=extent.width;height=extent.height;uint32_t count=caps.minImageCount+1;if(caps.maxImageCount&&count>caps.maxImageCount)count=caps.maxImageCount;
        VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,nullptr,0,surface,count,chosen.format,chosen.colorSpace,extent,1,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_SHARING_MODE_EXCLUSIVE,0,nullptr,caps.currentTransform,VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,kForcedVsyncPresentMode,VK_TRUE,0};vkcheck(CreateSwapchain(dev,&sc,nullptr,&swap),"vkCreateSwapchainKHR");uint32_t ic=0;GetSwapImages(dev,swap,&ic,nullptr);images.resize(ic);GetSwapImages(dev,swap,&ic,images.data());views.resize(ic);for(uint32_t i=0;i<ic;i++){VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,nullptr,0,images[i],VK_IMAGE_VIEW_TYPE_2D,format,{0,0,0,0},{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};vkcheck(CreateImageView(dev,&vi,nullptr,&views[i]),"image view");}
        VkAttachmentDescription ad{0,format,VK_SAMPLE_COUNT_1_BIT,VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,VK_ATTACHMENT_LOAD_OP_DONT_CARE,VK_ATTACHMENT_STORE_OP_DONT_CARE,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};VkAttachmentReference ar{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sp{0,VK_PIPELINE_BIND_POINT_GRAPHICS,0,nullptr,1,&ar,nullptr,nullptr,0,nullptr};VkSubpassDependency dep{VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0};VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,nullptr,0,1,&ad,1,&sp,1,&dep};vkcheck(CreateRenderPass(dev,&rp,nullptr,&renderPass),"render pass");fbs.resize(ic);for(uint32_t i=0;i<ic;i++){VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,nullptr,0,renderPass,1,&views[i],extent.width,extent.height,1};vkcheck(CreateFramebuffer(dev,&fi,nullptr,&fbs[i]),"framebuffer");}cmds.resize(ic);VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,nullptr,pool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,ic};vkcheck(AllocateCommandBuffers(dev,&cai,cmds.data()),"command buffers");resizePending=false;
    }
    void combined(GameButton b,bool k,bool j){auto i=static_cast<std::size_t>(b);bool before=input.down[i],after=k||j;if(after!=before){input.down[i]=after;if(after)input.pressed[i]=true;else input.released[i]=true;}}
    void refresh(GameButton b){auto i=static_cast<std::size_t>(b);combined(b,keyboardDown[i],joyDown[i]);}
    static bool keyMap(uint8_t code,GameButton& out){switch(code){
        case 25:case 111:out=GameButton::Up;return true;case 39:case 116:out=GameButton::Down;return true;case 38:case 113:out=GameButton::Left;return true;case 40:case 114:out=GameButton::Right;return true;
        case 26:case 36:case 52:case 65:out=GameButton::Interact;return true;case 9:case 53:out=GameButton::Menu;return true;case 50:case 62:out=GameButton::Run;return true;
        case 71:out=GameButton::Save;return true;case 75:out=GameButton::Load;return true;case 69:out=GameButton::Debug;return true;case 68:out=GameButton::Assets;return true;case 70:out=GameButton::Terrain;return true;case 27:out=GameButton::Reset;return true;case 24:out=GameButton::Quit;return true;default:return false;}}
    void keyEvent(uint8_t code,bool down){GameButton b; if(!keyMap(code,b))return;auto i=static_cast<std::size_t>(b);keyboardDown[i]=down;refresh(b);}
    void clearKeyboard(){keyboardDown.fill(false);for(std::size_t i=0;i<keyboardDown.size();i++)combined(static_cast<GameButton>(i),false,joyDown[i]);}
    void joySet(GameButton b,bool down){auto i=static_cast<std::size_t>(b);joyDown[i]=down;refresh(b);}
    void clearJoyDirections(){joySet(GameButton::Left,false);joySet(GameButton::Right,false);joySet(GameButton::Up,false);joySet(GameButton::Down,false);}
    void updateJoyAxis(){
        if(!joystickArmed){clearJoyDirections();return;}
        const int dead=16000;
        const int dx=int(joyX)-int(joyCenterX),dy=int(joyY)-int(joyCenterY);
        joySet(GameButton::Left,dx<-dead);joySet(GameButton::Right,dx>dead);joySet(GameButton::Up,dy<-dead);joySet(GameButton::Down,dy>dead);
    }
    void pollJoystick(){if(js<0)return;js_event e{};while(read(js,&e,sizeof(e))==sizeof(e)){bool init=(e.type&JS_EVENT_INIT)!=0;uint8_t type=e.type&~JS_EVENT_INIT;if(type==JS_EVENT_AXIS){if(e.number==0)joyX=e.value;else if(e.number==1)joyY=e.value;if(!init)updateJoyAxis();}else if(type==JS_EVENT_BUTTON){bool d=e.value!=0;if(!init&&d&&!joystickArmed){joystickArmed=true;joyCenterX=joyX;joyCenterY=joyY;std::cout<<"Controller input enabled; axes calibrated at center ("<<joyCenterX<<","<<joyCenterY<<")\n";updateJoyAxis();}if(init)continue;if(e.number==0)joySet(GameButton::Interact,d);else if(e.number==1)joySet(GameButton::Menu,d);else if(e.number==4||e.number==5)joySet(GameButton::Run,d);else if(e.number==6&&d)joySet(GameButton::Save,true),joySet(GameButton::Save,false);else if(e.number==7)joySet(GameButton::Menu,d);}}}
    void destroyStaging(){
        if(!dev)return;
        if(stagingMapped&&stagingMemory){UnmapMemory(dev,stagingMemory);stagingMapped=nullptr;}
        if(stagingBuffer){DestroyBuffer(dev,stagingBuffer,nullptr);stagingBuffer=0;}
        if(stagingMemory){FreeMemory(dev,stagingMemory,nullptr);stagingMemory=0;}
        stagingSize=0;
    }
    uint32_t memoryType(uint32_t bits,VkMemoryPropertyFlags want){
        VkPhysicalDeviceMemoryProperties mp{};GetMemoryProps(phys,&mp);
        for(uint32_t i=0;i<mp.memoryTypeCount;i++)if((bits&(1u<<i))&&((mp.memoryTypes[i].propertyFlags&want)==want))return i;
        throw std::runtime_error("no host-visible coherent Vulkan memory type for framebuffer staging");
    }
    void ensureStaging(VkDeviceSize need){
        if(stagingBuffer&&stagingSize>=need)return;
        if(DeviceWaitIdle)DeviceWaitIdle(dev);
        destroyStaging();
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,nullptr,0,need,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_SHARING_MODE_EXCLUSIVE,0,nullptr};
        vkcheck(CreateBuffer(dev,&bi,nullptr,&stagingBuffer),"vkCreateBuffer(staging)");
        VkMemoryRequirements mr{};GetBufferMemoryRequirements(dev,stagingBuffer,&mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,nullptr,mr.size,memoryType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        vkcheck(AllocateMemory(dev,&ai,nullptr,&stagingMemory),"vkAllocateMemory(staging)");
        vkcheck(BindBufferMemory(dev,stagingBuffer,stagingMemory,0),"vkBindBufferMemory(staging)");
        vkcheck(MapMemory(dev,stagingMemory,0,VK_WHOLE_SIZE,0,&stagingMapped),"vkMapMemory(staging)");
        stagingSize=need;
    }
    static std::uint8_t chan(float v){return std::uint8_t(std::clamp(v,0.0f,1.0f)*255.0f+0.5f);}
    void composePixels(const RenderFrame& f){
        const VkDeviceSize bytes=VkDeviceSize(extent.width)*extent.height*4;ensureStaging(bytes);
        auto* dst=static_cast<std::uint8_t*>(stagingMapped);
        std::memset(dst,0,std::size_t(bytes));
        float scale=std::min(float(extent.width)/RenderFrame::LogicalWidth,float(extent.height)/RenderFrame::LogicalHeight);
        float ox=(float(extent.width)-RenderFrame::LogicalWidth*scale)*0.5f,oy=(float(extent.height)-RenderFrame::LogicalHeight*scale)*0.5f;
        int vx0=std::max(0,int(std::floor(ox))),vy0=std::max(0,int(std::floor(oy)));
        int vx1=std::min(int(extent.width),int(std::ceil(ox+RenderFrame::LogicalWidth*scale)));
        int vy1=std::min(int(extent.height),int(std::ceil(oy+RenderFrame::LogicalHeight*scale)));
        const bool pixels=f.hasPixels();
        const std::uint8_t cr=chan(f.clear.r),cg=chan(f.clear.g),cb=chan(f.clear.b);
        for(int y=vy0;y<vy1;y++){
            int sy=std::clamp(int((float(y)+0.5f-oy)/scale),0,RenderFrame::PixelHeight-1);
            for(int x=vx0;x<vx1;x++){
                int sx=std::clamp(int((float(x)+0.5f-ox)/scale),0,RenderFrame::PixelWidth-1);
                std::size_t dq=(std::size_t(y)*extent.width+std::size_t(x))*4;
                if(pixels){
                    std::size_t sq=(std::size_t(sy)*RenderFrame::PixelWidth+std::size_t(sx))*4;
                    // Swapchain is B8G8R8A8.
                    dst[dq]=f.rgba[sq+2];dst[dq+1]=f.rgba[sq+1];dst[dq+2]=f.rgba[sq];dst[dq+3]=255;
                }else{dst[dq]=cb;dst[dq+1]=cg;dst[dq+2]=cr;dst[dq+3]=255;}
            }
        }
        auto R=[&](float x,float y,float w,float h,Color c){
            int x0=std::max(0,int(std::floor(ox+x*scale))),y0=std::max(0,int(std::floor(oy+y*scale)));
            int x1=std::min(int(extent.width),int(std::ceil(ox+(x+w)*scale))),y1=std::min(int(extent.height),int(std::ceil(oy+(y+h)*scale)));
            if(x1<=x0||y1<=y0)return;
            std::uint8_t b=chan(c.b),g=chan(c.g),r=chan(c.r);
            for(int py=y0;py<y1;py++)for(int px=x0;px<x1;px++){std::size_t q=(std::size_t(py)*extent.width+std::size_t(px))*4;dst[q]=b;dst[q+1]=g;dst[q+2]=r;dst[q+3]=255;}
        };
        for(auto const& r:f.rects)R(r.x,r.y,r.w,r.h,r.color);
        auto glyph=[&](float gx,float gy,int sc,char ch,Color c){auto const& rows=glyphFor(ch);for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)if(rows[yy]&(1u<<(4-xx)))R(gx+xx*sc,gy+yy*sc,float(sc),float(sc),c);};
        for(auto const& t:f.texts){float x=t.x,y=t.y;const float adv=t.advance>0.0f?t.advance:float(t.scale*6);const float line=t.lineAdvance>0.0f?t.lineAdvance:float(t.scale*9);for(char ch:t.text){if(ch=='\n'){x=t.x;y+=line;continue;}if(t.shadow&&ch!=' ')glyph(x+t.scale,y+t.scale,t.scale,ch,{0.015f,0.02f,0.025f,1});if(ch!=' ')glyph(x,y,t.scale,ch,t.color);x+=adv;}}
    }
    void recordPixels(uint32_t idx,const RenderFrame& frame){
        composePixels(frame);
        auto cb=cmds[idx];ResetCommandBuffer(cb,0);VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,nullptr,0,nullptr};vkcheck(BeginCommandBuffer(cb,&bi),"begin command");
        VkImageMemoryBarrier a{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,nullptr,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,images[idx],{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&a);
        VkBufferImageCopy copy{0,0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},{0,0,0},{extent.width,extent.height,1}};
        CmdCopyBufferToImage(cb,stagingBuffer,images[idx],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,nullptr,VK_ACCESS_TRANSFER_WRITE_BIT,0,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,images[idx],{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&b);
        vkcheck(EndCommandBuffer(cb),"end command");
    }
    void clearRect(VkCommandBuffer cb,int x,int y,int w,int h,Color c){if(w<=0||h<=0||x>=int(extent.width)||y>=int(extent.height)||x+w<=0||y+h<=0)return;int x0=std::max(0,x),y0=std::max(0,y),x1=std::min(int(extent.width),x+w),y1=std::min(int(extent.height),y+h);if(x1<=x0||y1<=y0)return;VkClearAttachment a{};a.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;a.colorAttachment=0;a.clearValue.color.float32[0]=c.r;a.clearValue.color.float32[1]=c.g;a.clearValue.color.float32[2]=c.b;a.clearValue.color.float32[3]=c.a;VkClearRect cr{{{x0,y0},{uint32_t(x1-x0),uint32_t(y1-y0)}},0,1};CmdClearAttachments(cb,1,&a,1,&cr);}
    void drawFrame(VkCommandBuffer cb,const RenderFrame& f){
        float s=std::min(float(extent.width)/RenderFrame::LogicalWidth,float(extent.height)/RenderFrame::LogicalHeight);float ox=(float(extent.width)-RenderFrame::LogicalWidth*s)*0.5f,oy=(float(extent.height)-RenderFrame::LogicalHeight*s)*0.5f;
        auto R=[&](float x,float y,float w,float h,Color c){clearRect(cb,int(std::floor(ox+x*s)),int(std::floor(oy+y*s)),int(std::ceil(w*s))+1,int(std::ceil(h*s))+1,c);};
        for(auto const& r:f.rects)R(r.x,r.y,r.w,r.h,r.color);
        auto glyph=[&](float gx,float gy,int scale,char ch,Color c){auto const& rows=glyphFor(ch);for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)if(rows[yy]&(1u<<(4-xx)))R(gx+xx*scale,gy+yy*scale,float(scale),float(scale),c);};
        for(auto const& t:f.texts){float x=t.x,y=t.y;const float adv=t.advance>0.0f?t.advance:float(t.scale*6);const float line=t.lineAdvance>0.0f?t.lineAdvance:float(t.scale*9);for(char ch:t.text){if(ch=='\n'){x=t.x;y+=line;continue;}if(t.shadow&&ch!=' ')glyph(x+t.scale,y+t.scale,t.scale,ch,{0.015f,0.02f,0.025f,1});if(ch!=' ')glyph(x,y,t.scale,ch,t.color);x+=adv;}}
    }
    void record(uint32_t idx,const RenderFrame& frame){
        if(frame.hasPixels()){recordPixels(idx,frame);return;}
        auto cb=cmds[idx];ResetCommandBuffer(cb,0);VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,nullptr,0,nullptr};vkcheck(BeginCommandBuffer(cb,&bi),"begin command");VkClearValue base{};base.color.float32[0]=frame.clear.r;base.color.float32[1]=frame.clear.g;base.color.float32[2]=frame.clear.b;base.color.float32[3]=frame.clear.a;VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,nullptr,renderPass,fbs[idx],{{0,0},extent},1,&base};CmdBeginRenderPass(cb,&rbi,0);drawFrame(cb,frame);CmdEndRenderPass(cb);vkcheck(EndCommandBuffer(cb),"end command");
    }
    bool draw(const RenderFrame& frame){if(resizePending&&width>0&&height>0){destroySwapchain();createSwapchain();}uint32_t idx=0;VkResult ar=Acquire(dev,swap,~0ull,imageAvail,0,&idx);if(ar==VK_ERROR_OUT_OF_DATE_KHR){destroySwapchain();createSwapchain();return true;}vkcheck(ar,"acquire");record(idx,frame);VkPipelineStageFlags stage=frame.hasPixels()?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO,nullptr,1,&imageAvail,&stage,1,&cmds[idx],1,&renderDone};vkcheck(QueueSubmit(queue,1,&si,0),"queue submit");VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,nullptr,1,&renderDone,1,&swap,&idx,nullptr};VkResult pr=QueuePresent(queue,&pi);QueueWaitIdle(queue);if(pr==VK_ERROR_OUT_OF_DATE_KHR||pr==VK_SUBOPTIMAL_KHR){destroySwapchain();createSwapchain();}else vkcheck(pr,"present");return true;}
    void cleanup(){if(js>=0){close(js);js=-1;}if(dev&&DeviceWaitIdle)DeviceWaitIdle(dev);destroySwapchain();destroyStaging();if(dev&&imageAvail)DestroySemaphore(dev,imageAvail,nullptr);if(dev&&renderDone)DestroySemaphore(dev,renderDone,nullptr);if(dev&&pool)DestroyCommandPool(dev,pool,nullptr);if(dev&&DestroyDevice)DestroyDevice(dev,nullptr);dev=nullptr;if(instance&&surface&&DestroySurface)DestroySurface(instance,surface,nullptr);surface=0;if(instance&&DestroyInstance)DestroyInstance(instance,nullptr);instance=nullptr;if(vkLib)dlclose(vkLib);vkLib=nullptr;if(xc){if(win)xcb_destroy_window(xc,win);xcb_disconnect(xc);}xc=nullptr;win=0;}
};

VulkanXcbRenderer::VulkanXcbRenderer():p(std::make_unique<Impl>()){}
VulkanXcbRenderer::~VulkanXcbRenderer()=default;
bool VulkanXcbRenderer::init(uint32_t w,uint32_t h,const std::string& title){try{p->width=w;p->height=h;p->makeWindow(title);p->initVulkan();std::cout<<"Vulkan native renderer initialized ("<<p->extent.width<<"x"<<p->extent.height<<")\n";return true;}catch(const std::exception& e){std::cerr<<"Renderer init failed: "<<e.what()<<"\n";return false;}}
int VulkanXcbRenderer::run(NativeGame& game){bool running=true;auto last=std::chrono::steady_clock::now();while(running&&!game.wantsQuit()){p->input.clearEdges();while(auto* ev=xcb_poll_for_event(p->xc)){uint8_t t=ev->response_type&0x7f;if(t==XCB_KEY_PRESS){auto* k=reinterpret_cast<xcb_key_press_event_t*>(ev);p->keyEvent(k->detail,true);}else if(t==XCB_KEY_RELEASE){auto* k=reinterpret_cast<xcb_key_release_event_t*>(ev);p->keyEvent(k->detail,false);}else if(t==XCB_MOTION_NOTIFY){auto* m=reinterpret_cast<xcb_motion_notify_event_t*>(ev);float sc=std::min(float(p->width)/RenderFrame::LogicalWidth,float(p->height)/RenderFrame::LogicalHeight);float ox=(float(p->width)-RenderFrame::LogicalWidth*sc)*0.5f,oy=(float(p->height)-RenderFrame::LogicalHeight*sc)*0.5f;p->input.mouseX=(float(m->event_x)-ox)/sc;p->input.mouseY=(float(m->event_y)-oy)/sc;p->input.mouseInside=p->input.mouseX>=0&&p->input.mouseY>=0&&p->input.mouseX<RenderFrame::LogicalWidth&&p->input.mouseY<RenderFrame::LogicalHeight;}else if(t==XCB_BUTTON_PRESS){auto* b=reinterpret_cast<xcb_button_press_event_t*>(ev);if(b->detail==1){p->input.mouseDown=true;p->input.mousePressed=true;}}else if(t==XCB_BUTTON_RELEASE){auto* b=reinterpret_cast<xcb_button_release_event_t*>(ev);if(b->detail==1){p->input.mouseDown=false;p->input.mouseReleased=true;}}else if(t==XCB_FOCUS_OUT){p->clearKeyboard();p->input.mouseDown=false;}else if(t==XCB_CLIENT_MESSAGE){auto* c=reinterpret_cast<xcb_client_message_event_t*>(ev);if(c->data.data32[0]==p->wmDelete)running=false;}else if(t==XCB_CONFIGURE_NOTIFY){auto* c=reinterpret_cast<xcb_configure_notify_event_t*>(ev);if(c->width&&c->height&&(c->width!=p->width||c->height!=p->height)){p->width=c->width;p->height=c->height;p->resizePending=true;}}free(ev);}p->pollJoystick();auto now=std::chrono::steady_clock::now();double dt=std::chrono::duration<double>(now-last).count();last=now;game.update(p->input,dt);if(!running||game.wantsQuit())break;try{p->draw(game.render());}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}}return 0;}
