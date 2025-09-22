/*
 * This file is part of PRO CFW.

 * PRO CFW is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * PRO CFW is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PRO CFW. If not, see <http://www.gnu.org/licenses/ .
 */

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <pspsdk.h>
#include <pspsysmem_kernel.h>
#include <pspkernel.h>
#include <psputilsforkernel.h>
#include <pspsysevent.h>
#include <pspiofilemgr.h>
#include <pspctrl.h>
#include <pspumd.h>

#include <ark.h>
#include <cfwmacros.h>
#include <vshctrl.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <systemctrl_private.h>

#include "main.h"
#include "xmbiso.h"
#include "virtual_pbp.h"
#include "custom_update.h"

extern int _sceCtrlReadBufferPositive(SceCtrlData *ctrl, int count);
extern void patch_sceUSB_Driver(void);

extern int (*g_sceCtrlReadBufferPositive) (SceCtrlData *, int);

typedef struct _HookUserFunctions {
    u32 nid;
    void *func;
} HookUserFunctions;

static STMOD_HANDLER previous;

static void patch_sysconf_plugin_module(SceModule *mod);
static void patch_game_plugin_module(SceModule *mod);
static void patch_vsh_module(SceModule * mod);

static void patch_sceCtrlReadBufferPositive(void); 
static void patch_Gameboot(SceModule *mod); 
static void patch_hibblock(SceModule *mod); 
static void patch_msvideo_main_plugin_module(SceModule* mod);

extern SEConfig* se_config;

extern int has_umd_iso;

static void vshpatch_module_chain(SceModule *mod)
{

    if(0 == strcmp(mod->modname, "sysconf_plugin_module")) {
        patch_sysconf_plugin_module(mod);
        goto exit;
    }

    if(0 == strcmp(mod->modname, "game_plugin_module")) {
        patch_game_plugin_module(mod);
        goto exit;
    }

    if(0 == strcmp(mod->modname, "vsh_module")) {
        patch_vsh_module(mod);
        patch_sceCtrlReadBufferPositive();
        goto exit;
    }

    if(0 == strcmp(mod->modname, "msvideo_main_plugin_module")) {
        patch_msvideo_main_plugin_module(mod);
        goto exit;
    }

    if( 0 == strcmp(mod->modname, "update_plugin_module")) {
        patch_update_plugin_module(mod);
        goto exit;
    }

    if(0 == strcmp(mod->modname, "SceUpdateDL_Library")) {
        patch_SceUpdateDL_Library(mod);
        goto exit;
    }

    if(0 == strcmp(mod->modname, "sceVshBridge_Driver")) {
        
        if (se_config->skiplogos == 1 || se_config->skiplogos == 2){
            patch_Gameboot(mod);
        }

        if (psp_model == PSP_GO && se_config->hibblock) {
        	patch_hibblock(mod);
        }

        goto exit;
    }

exit:
    sctrlFlushCache();
    if (previous) previous(mod);
}

static int (*prev_start)(int modid, SceSize argsize, void * argp, int * modstatus, SceKernelSMOption * opt) = NULL;
static int StartModuleHandler(int modid, SceSize argsize, void * argp, int * modstatus, SceKernelSMOption * opt){

    SceModule* mod = (SceModule*) sceKernelFindModuleByUID(modid);

    if (mod && strcmp(mod->modname, "vsh_module") == 0) {
        // Load XMB Control
        SceUID modid = sceKernelLoadModule(XMBCTRL_PRX_FLASH, 0, NULL);
        if (modid >= 0) sceKernelStartModule(modid, 0, NULL, NULL, NULL);
    }

    // forward to previous or default StartModule
    if (prev_start) return prev_start(modid, argsize, argp, modstatus, opt);
    return -1;
}

static void patch_sceCtrlReadBufferPositive(void)
{
    SceModule* mod = (SceModule*)sceKernelFindModuleByName("sceVshBridge_Driver");
    sctrlHookImportByNID(mod, "sceCtrl_driver", 0xBE30CED0, _sceCtrlReadBufferPositive);
    g_sceCtrlReadBufferPositive = (void *) sctrlHENFindFunction("sceController_Service", "sceCtrl", 0x1F803938);
    sctrlHENPatchSyscall(g_sceCtrlReadBufferPositive, _sceCtrlReadBufferPositive);
}

static void patch_Gameboot(SceModule *mod)
{
    sctrlHookImportByNID(mod, "sceDisplay_driver", 0x3552AB11, 0);
}

static void patch_hibblock(SceModule *mod)
{
    u32 text_addr = mod->text_addr;
    u32 top_addr = text_addr + mod->text_size;
    for (u32 addr=text_addr; addr<top_addr; addr+=4){
        if (_lw(addr) == 0x7C022804){
            MAKE_DUMMY_FUNCTION_RETURN_0(addr - 8);
            break;
        }
    }
}

static inline void ascii2utf16(char *dest, const char *src)
{
    while(*src != '\0') {
        *dest++ = *src;
        *dest++ = '\0';
        src++;
    }
    *dest++ = '\0';
    *dest++ = '\0';
}

static const char *g_cfw_dirs[] = {
    "/SEPLUGINS",
    "/ISO",
    "/ISO/VIDEO",
};

int myIoMkdir(const char *dir, SceMode mode)
{
    int ret, i;
    u32 k1;
    if(0 == strcmp(dir, "ms0:/PSP/GAME") || 
            0 == strcmp(dir, "ef0:/PSP/GAME")) {
        k1 = pspSdkSetK1(0);
        for(i=0; i<NELEMS(g_cfw_dirs); ++i) {
            char path[64];
            get_device_name(path, sizeof(path), dir);
            strcat(path, g_cfw_dirs[i]);
            sceIoMkdir(path, mode);
        }
        pspSdkSetK1(k1);
    }
    ret = sceIoMkdir(dir, mode);
    return ret;
}

static void patch_sysconf_plugin_module(SceModule *mod)
{
    u32 text_addr = mod->text_addr;
    u32 top_addr = text_addr+mod->text_size;
    u32 a = 0;
    u32 addr;
    char str[50];

    SceIoStat stat; int test_fd = sceIoGetstat("flash0:/vsh/resource/13-27.bmp", &stat);

    int patches = (psp_model==PSP_1000 && test_fd >= 0)? 2 : 1;
    for (addr=text_addr; addr<top_addr && patches; addr+=4){
        if (_lw(addr) == 0x34C600C9 && _lw(addr+8) == NOP){
            a = addr+20;
            patches--;
        }
        else if (psp_model == PSP_1000 && test_fd >= 0 && _lw(addr) == 0x26530008 && _lw(addr-4) == 0x24040018){
            // allow slim colors in PSP 1K
            u32 patch_addr, value;

            patch_addr = addr-28;
            value = *(u32 *)(patch_addr + 4);

            _sw(0x24020001, patch_addr + 4);
            _sw(value,  patch_addr);

            patches--;
        }
    }
    
    if (se_config->hidemac){
        for (; addr < top_addr; addr++){
            if (   ((u8*)addr)[0] == 0x25
                && ((u8*)addr)[1] == 0
                && ((u8*)addr)[2] == 0x30
                && ((u8*)addr)[3] == 0
                && ((u8*)addr)[4] == 0x32
                && ((u8*)addr)[5] == 0
                && ((u8*)addr)[6] == 0x58
                && ((u8*)addr)[7] == 0 )
            {
                char model[20];
                if (IS_VITA_ADR(ark_config)){
                    model[0]='v'; model[1]='P'; model[2]='S'; model[3]='P'; model[4]=0;
                }
                else{
                    sprintf(model, "%02dg", (int)psp_model+1);
                }
                sprintf(str, " [ Model: %s ] ", model);
                ascii2utf16((char*)addr, str);
                break;
            }
        }
    }
    
    u32 fw = sceKernelDevkitVersion();
    u32 major = fw>>24;
    u32 minor = (fw>>16)&0xF;
    u32 micro = (fw>>8)&0xF;
    char* tool = "";
    switch (sctrlHENIsToolKit()){
        case 1: tool = "TT"; break;
        case 2: tool = "DT"; break;
    }

    static void* p = NULL;
    if (!p) p = user_malloc(50);
    
    sprintf(str, "%d.%d%d%s ARK-%d %s", (int)major, (int)minor, (int)micro, tool, ARK_MAJOR_VERSION, ark_config->exploit_id);
    ascii2utf16(p, str);
    
    _sw(0x3C020000 | ((u32)(p) >> 16), a); // lui $v0, 
    _sw(0x34420000 | ((u32)(p) & 0xFFFF), a + 4); // or $v0, $v0, 

    sctrlHookImportByNID((SceModule*)mod, "IoFileMgrForUser", 0x06A70004, myIoMkdir);
}

int fakeParamInexistance(void)
{
    return 0x80120005;
}

static void patch_game_plugin_module(SceModule* mod)
{
    u32 text_addr = mod->text_addr;
    u32 top_addr = text_addr + mod->text_size;
    int patches = 5;
    if (se_config->hidepics) patches++;
    for (u32 addr=text_addr; addr<top_addr && patches; addr+=4){
        u32 data = _lw(addr);
        if (data == 0x8FA400A4){
            //disable executable check for normal homebrew
            u32 p = U_EXTRACT_CALL(addr+40);
            MAKE_DUMMY_FUNCTION_RETURN_0(p);
            patches--;
        }
        else if (data == 0xAFB000E0){
            //kill ps1 eboot check
            MAKE_DUMMY_FUNCTION_RETURN_0(addr-4);
            patches--;
        }
        else if (data == 0x27A701C8){
            //kill multi-disc ps1 check
            _sw(NOP, addr+16);
            patches--;
        }
        else if (data == 0x27BD10F0){
            // disable check for custom psx eboot restore 
            // rif file check
            _sw(0x00001021, addr + 4);
            if (_lw(addr+48) == 0x3463850E){
                // rif content memcmp check
                _sw(NOP, addr+44);
                // some type check, branch it
                _sw(0x10000010, addr + 64);
            }
            patches--;
        }
        else if (data == 0x3C028000){
            _sw(0x00001021, addr-24);
            patches--;
        }
        else if (data == 0x0062A023 && se_config->hidepics){
            if (se_config->hidepics == 1 || se_config->hidepics == 2)
                _sw(0x00601021, addr+36); // PIC0
            if (se_config->hidepics == 1 || se_config->hidepics == 3)
                _sw(0x00601021, addr+48); // PIC1
            patches--;
        }
    }
}

static void patch_msvideo_main_plugin_module(SceModule* mod)
{
    u32 text_addr = mod->text_addr;
    u32 top_addr = text_addr + mod->text_size;
    int patches = 10;

    for (u32 addr=text_addr; addr<top_addr && patches; addr+=4){
        u32 data = _lw(addr);
        if ((data & 0xFF00FFFF) == 0x34002C00){
            /* Patch resolution limit to (130560) pixels (480x272) */
            _sh(0xFE00, addr);
            patches--;
        }
        else if (data == 0x2C420303 || data == 0x2C420FA1){
            /* Patch bitrate limit (increase to 16384+2) */
            _sh(0x4003, addr);
            patches--;
        }
    }
}

int umdLoadExec(char * file, struct SceKernelLoadExecVSHParam * param)
{
    //result
    int ret = 0;

    sctrlSESetDiscType(PSP_UMD_TYPE_GAME);

    if(psp_model == PSP_GO) {
        char devicename[20];
        int apitype;

        extern char mounted_iso[64];
        file = mounted_iso;
        ret = get_device_name(devicename, sizeof(devicename), file);

        if(ret == 0 && 0 == strcasecmp(devicename, "ef0:")) {
            apitype = 0x125;
        } else {
            apitype = 0x123;
        }

        param->key = "umdemu";
        sctrlSESetBootConfFileIndex(MODE_INFERNO);
        ret = sctrlKernelLoadExecVSHWithApitype(apitype, file, param);
    } else {
        sctrlSESetBootConfFileIndex(MODE_UMD);
        sctrlSESetUmdFile("");
        int apitype = (strstr(param->argp, "/PBOOT.PBP")==NULL)? 0x120:0x160;
        ret = sctrlKernelLoadExecVSHWithApitype(apitype, file, param);
    }

    return ret;
}

int umdLoadExecUpdater(char * file, struct SceKernelLoadExecVSHParam * param)
{
    //result
    int ret = 0;
    sctrlSESetBootConfFileIndex(MODE_UPDATERUMD);
    sctrlSESetDiscType(PSP_UMD_TYPE_GAME);
    ret = sceKernelLoadExecVSHDiscUpdater(file, param);
    return ret;
}

static void do_pspgo_umdvideo_patch(u32 addr){
    u32 prev = _lw(addr + 4);
    _sw(prev, addr);
    _sw(0x24020000 | PSP_4000, addr + 4);
}

static void patch_vsh_module_for_pspgo_umdvideo(SceModule *mod)
{
    if (!has_umd_iso) return;
    u32 text_addr = mod->text_addr;
    u32 top_addr = text_addr + mod->text_size;
    int patches = 3;
    for (u32 addr=text_addr; addr<top_addr && patches; addr+=4){
        u32 data = _lw(addr);
        if (data == 0x38840001){
            do_pspgo_umdvideo_patch(addr+40);
            patches--;
        }
        else if (data == 0x3C0500FF){
            do_pspgo_umdvideo_patch(addr-48);
            patches--;
        }
        else if (data == 0x02821007){
            do_pspgo_umdvideo_patch(addr-56);
            patches--;
        }
    }
}

static void patch_vsh_module(SceModule * mod)
{
    //enable homebrew boot
    u32 top_addr = mod->text_addr+mod->text_size;
    u32 fakeparam = SYSCALL(sctrlKernelQuerySystemCall(fakeParamInexistance));
    int patches = 6;
    for (u32 addr=mod->text_addr; addr<top_addr && patches; addr+=4){
        u32 data = _lw(addr);
        if (data == 0x27A602EC){
            _sw(NOP, addr+16);
            patches--;
        }
        else if (data == 0x83A2006C && _lw(addr-8) == 0x2407000D){
            _sw(NOP, addr-4);
            _sw(NOP, addr+4);
            patches--;
        }
        else if (data == 0x93A40060){
            _sw(fakeparam, addr-76);
            patches--;
        }
        else if (data == 0x27B60080){
            _sw(fakeparam, addr+16);
            patches--;
        }
        else if (data == 0x8FA30068){
            _sw(fakeparam, addr-72);
            patches--;
        }
        else if (data == 0xAFA000AC){
            _sw(fakeparam, addr-60);
            patches--;
        }
        else if (data == 0x2C430004 && psp_model == PSP_GO){
            // allow PSP Go to use Type 1 Updaters
        	_sw( 0x24030002 , addr - 8 ); //addiu      $v1, $zr, 2
        }
        
    }

    sctrlHookImportByNID((SceModule*)mod, "sceVshBridge", 0x21D4D038, homebrewloadexec);
    sctrlHookImportByNID((SceModule*)mod, "sceVshBridge", 0xE533E98C, homebrewloadexec);

    u32 vshloadexec_nids[] = {0xB8B07CAF, 0x791FCD43, 0x01730088, 0x5B7F3339};
    for (int i=0; i<NELEMS(vshloadexec_nids); i++){
        sctrlHookImportByNID((SceModule*)mod, "sceVshBridge", vshloadexec_nids[i], umdemuloadexec);
    }
    
    sctrlHookImportByNID((SceModule*)mod, "sceVshBridge", 0x63E69956, umdLoadExec);
    sctrlHookImportByNID((SceModule*)mod, "sceVshBridge", 0x0C0D5913, umdLoadExec);
    sctrlHookImportByNID((SceModule*)mod, "sceVshBridge", 0x81682A40, umdLoadExecUpdater);
    
    if(psp_model == PSP_GO && has_umd_iso) {
        patch_vsh_module_for_pspgo_umdvideo(mod);
    }

    if (se_config->skiplogos == 1 || se_config->skiplogos == 2){
        // patch GameBoot
        sctrlHookImportByNID((SceModule*)sceKernelFindModuleByName("sceVshBridge_Driver"), "sceDisplay_driver", 0x3552AB11, 0);
    }

    #if 0
    _sb(0, mod->text_addr+0x1FF84); // enable xmb editing
    _sb(7, mod->text_addr+0x54DC9); // unlock psn sign up item
    _sb(7, mod->text_addr+0x54DD9); // unlock psn store item
    _sb(7, mod->text_addr+0x54DE9); // unlock psn board item
    _sb(7, mod->text_addr+0x54F65); // unlock psn sign up icon
    _sb(7, mod->text_addr+0x54F79); // unlock psn store icon
    _sb(7, mod->text_addr+0x54FC9); // unlock psn board icon
    #endif

}

static void hook_iso_io(void)
{
    HookUserFunctions hook_list[] = {
        { 0x109F50BC, gameopen,    },
        { 0x6A638D83, gameread,    },
        { 0x810C4BC3, gameclose,   },
        { 0x27EB27B8, gamelseek,   },
        { 0xACE946E8, gamegetstat, },
        { 0xF27A9C51, gameremove,  },
        { 0x1117C65F, gamermdir,   },
        { 0x779103A0, gamerename,  },
        { 0xB8A740F4, gamechstat,  },
        { 0xB29DDF9C, gamedopen    },
        { 0xE3EB004C, gamedread    },
        { 0xEB092469, gamedclose   },
    };
    for(int i=0; i<NELEMS(hook_list); ++i) {
        void *fp = (void*)sctrlHENFindFunction("sceIOFileManager", "IoFileMgrForUser", hook_list[i].nid);
        if(fp != NULL) {
            sctrlHENPatchSyscall(fp, hook_list[i].func);
        }
    }
}

int vshpatch_init(void)
{
    previous = sctrlHENSetStartModuleHandler(vshpatch_module_chain);
    if (sceKernelFindModuleByName("XmbControl") == NULL){
        prev_start = sctrlSetStartModuleExtra(StartModuleHandler);
    }
    patch_sceUSB_Driver();
    vpbp_init();
    hook_iso_io();
    return 0;
}
