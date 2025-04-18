﻿#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <dlfcn.h>
#include <signal.h>

#include <memory>
#include <asm/ptrace.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <map>

#include "kernel_root_kit_process64_inject.h"
#include "kernel_root_kit_ptrace_arm64_utils.h"
#include "kernel_root_kit_maps_helper.h"
#include "kernel_root_kit_command.h"
#include "kernel_root_kit_elf64_symbol_parser.h"
#include "kernel_root_kit_log.h"

namespace kernel_root {

int _load_libc64_run_cmd_func_addr(
	const char* so_path,
	api_offset_read_mode api_mode,
	size_t& p_mmap_offset,
	size_t& p_munmap_offset,
	size_t& p_chdir_offset,
	size_t& p_clearenv_offset,
	size_t& p_setenv_offset,
	size_t& p_execve_offset,
	size_t& p_fileno_offset,
	size_t& p_popen_offset,
	size_t& p_pclose_offset,
	size_t& p_read_offset) {

	int ret = 0;
	std::map<std::string, uint64_t> func_symbol_map;
	func_symbol_map["mmap"] = 0;
	func_symbol_map["munmap"] = 0;
	func_symbol_map["chdir"] = 0;
	func_symbol_map["clearenv"] = 0;
	func_symbol_map["setenv"] = 0;
	func_symbol_map["execve"] = 0;
	func_symbol_map["fileno"] = 0;
	func_symbol_map["popen"] = 0;
	func_symbol_map["pclose"] = 0;
	func_symbol_map["read"] = 0;

	do {
		if(api_mode == api_offset_read_mode::only_read_myself_mem || api_mode == api_offset_read_mode::all) {
			bool is_already_loaded = !!get_module_base(-1, so_path);
			if (is_already_loaded) {
				ROOT_PRINTF("myself have this so.\n");
				ret = find_mem_elf64_symbol_address(so_path, func_symbol_map);
				break;
			}
		}
		if(api_mode == api_offset_read_mode::only_read_file || api_mode == api_offset_read_mode::all) {
			ret = read_elf64_file_symbol_addr(so_path, func_symbol_map);
			break;
		}
	} while (0);
	
	p_mmap_offset = func_symbol_map["mmap"];
	p_munmap_offset = func_symbol_map["munmap"];
	p_chdir_offset = func_symbol_map["chdir"];
	p_clearenv_offset = func_symbol_map["clearenv"];
	p_setenv_offset = func_symbol_map["setenv"];
	p_execve_offset = func_symbol_map["execve"];
	p_fileno_offset = func_symbol_map["fileno"];
	p_popen_offset = func_symbol_map["popen"];
	p_pclose_offset = func_symbol_map["pclose"];
	p_read_offset = func_symbol_map["read"];
	return ret;
}

int _load_libc64_modify_env_func_addr(
	const char* str_root_key,
	const char* so_path,
	api_offset_read_mode api_mode,
	size_t& p_mmap_offset,
	size_t& p_munmap_offset,
	size_t& p_getenv_offset,
	size_t& p_setenv_offset) {

	int ret = 0;
	std::map<std::string, uint64_t> func_symbol_map;
	func_symbol_map["getenv"] = 0;
	func_symbol_map["setenv"] = 0;
	func_symbol_map["mmap"] = 0;
	func_symbol_map["munmap"] = 0;

	do {
		if(api_mode == api_offset_read_mode::only_read_myself_mem || api_mode == api_offset_read_mode::all) {
			bool is_already_loaded = !!get_module_base(-1, so_path);
			if (is_already_loaded) {
				ROOT_PRINTF("myself have this so.\n");
				ret = find_mem_elf64_symbol_address(so_path, func_symbol_map);
				break;
			}
		}
		if(api_mode == api_offset_read_mode::only_read_file || api_mode == api_offset_read_mode::all) {
			ret = read_elf64_file_symbol_addr(so_path, func_symbol_map);
			break;
		}
	} while (0);
	
	p_mmap_offset = func_symbol_map["mmap"];
	p_munmap_offset = func_symbol_map["munmap"];
	p_getenv_offset = func_symbol_map["getenv"];
	p_setenv_offset = func_symbol_map["setenv"];
	return ret;
}

//远程注入  
std::string inject_process64_run_cmd(
	const char* str_root_key,
	pid_t target_pid,
	const char* libc64_so_path,
	size_t& p_mmap_offset,
	size_t& p_munmap_offset,
	size_t& p_chdir_offset,
	size_t& p_clearenv_offset,
	size_t& p_setenv_offset,
	size_t& p_execve_offset,
	size_t& p_fileno_offset,
	size_t& p_popen_offset,
	size_t& p_pclose_offset,
	size_t& p_read_offset,
	const char* cmd,
	ssize_t &out_err,
	bool user_root_auth = true,
	const char* chdir_path = NULL,
	bool clear_env = false,
	std::vector<process64_env>* set_env = NULL) {
	const char * str_r = "r";
	size_t remote_alloc_buf_size = 0;
	size_t remote_libc64_handle = 0;
	size_t mmap_addr, munmap_addr, chdir_addr, clearenv_addr, setenv_addr, execve_addr, fileno_addr, popen_addr, pclose_addr, read_addr;
	uint8_t* map_base;

	struct pt_regs regs, original_regs, erron_regs;
	unsigned long parameters[10];

	ssize_t remote_really_read = 0;
	FILE * fp_cmd;
	int pip_fp;
	std::string cmd_exec_result;

	out_err = -230;
	remote_alloc_buf_size = ((strlen(cmd) + 1 + strlen(str_r) + 1) / getpagesize()) * getpagesize();
	if((strlen(cmd) + 1 + strlen(str_r) + 1) % getpagesize()) {
		remote_alloc_buf_size += getpagesize();
	}
	if(remote_alloc_buf_size == 0) {
		remote_alloc_buf_size = getpagesize();
	}

	std::shared_ptr<char> sp_out_shell_buf(new (std::nothrow) char[remote_alloc_buf_size], std::default_delete<char[]>());
	if(!sp_out_shell_buf) {
		goto _ret;
	}
	memset(sp_out_shell_buf.get(), 0, remote_alloc_buf_size);

	ROOT_PRINTF("[+] Injecting process: %d\n", target_pid);

	//①ATTATCH，指定目标进程，开始调试  
	if (ptrace_attach(target_pid) == -1) {
		goto _ret;
	}

	//②GETREGS，获取目标进程的寄存器，保存现场  
	if (ptrace_getregs(target_pid, &regs) == -1) {
		goto _deatch;
	}
	/* save original registers */
	memcpy(&original_regs, &regs, sizeof(regs));

	//③获取目的进程的mmap函数的地址，以便为libxxx.so分配内存  

	/*
		需要对(void*)mmap进行说明：这是取得inject本身进程的mmap函数的地址，由于mmap函数在libc.so
		库中，为了将libxxx.so加载到目的进程中，就需要使用目的进程的mmap函数，所以需要查找到libc.so库在目的进程的起始地址。
	*/

	//获取远程pid的某个模块的起始地址  
	remote_libc64_handle = (size_t)get_module_base(target_pid, libc64_so_path);
	if (remote_libc64_handle == 0) {
		ROOT_PRINTF("[+] get_module_base failed.\n");
		goto _deatch;
	}
	mmap_addr = p_mmap_offset ? remote_libc64_handle + p_mmap_offset : 0;
	munmap_addr = p_munmap_offset ? remote_libc64_handle + p_munmap_offset : 0;
	chdir_addr = p_chdir_offset ? remote_libc64_handle + p_chdir_offset : 0;
	clearenv_addr = p_clearenv_offset ? remote_libc64_handle + p_clearenv_offset : 0;
	setenv_addr = p_setenv_offset ? remote_libc64_handle + p_setenv_offset : 0;
	execve_addr = p_execve_offset ? remote_libc64_handle + p_execve_offset : 0;
	fileno_addr = p_fileno_offset ? remote_libc64_handle + p_fileno_offset : 0;
	popen_addr = p_popen_offset ? remote_libc64_handle + p_popen_offset : 0;
	pclose_addr = p_pclose_offset ? remote_libc64_handle + p_pclose_offset : 0;
	read_addr = p_read_offset ? remote_libc64_handle + p_read_offset : 0;

	ROOT_PRINTF("[+] Remote mmap address: %p\n", (void*)mmap_addr);
	ROOT_PRINTF("[+] Remote munmap address: %p\n", (void*)munmap_addr);
	ROOT_PRINTF("[+] Remote chdir address: %p\n", (void*)p_chdir_offset);
	ROOT_PRINTF("[+] Remote clearenv address: %p\n", (void*)p_clearenv_offset);
	ROOT_PRINTF("[+] Remote setenv address: %p\n", (void*)p_setenv_offset);
	ROOT_PRINTF("[+] Remote execve address: %p\n", (void*)execve_addr);
	ROOT_PRINTF("[+] Remote fileno address: %p\n", (void*)fileno_addr);
	ROOT_PRINTF("[+] Remote popen address: %p\n", (void*)popen_addr);
	ROOT_PRINTF("[+] Remote pclose address: %p\n", (void*)pclose_addr);
	ROOT_PRINTF("[+] Remote read address: %p\n", (void*)read_addr);



	/* call mmap (null, 0x4000, PROT_READ | PROT_WRITE | PROT_EXEC,
							 MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	匿名申请一块0x4000大小的内存
	*/
	parameters[0] = 0;  // addr      
	parameters[1] = (unsigned long)remote_alloc_buf_size; // size
	parameters[2] = PROT_READ | PROT_WRITE | PROT_EXEC;  // prot      
	parameters[3] = MAP_ANONYMOUS | MAP_PRIVATE; // flags      
	parameters[4] = 0; //fd      
	parameters[5] = 0; //offset

	if (ptrace_call_wrapper(target_pid, "mmap", (void*)mmap_addr, parameters, 6, &regs) == -1) {
		goto _recovery;
	}

	//⑤从寄存器中获取mmap函数的返回值，即申请的内存首地址：  
	map_base = (uint8_t*)ptrace_retval(&regs);
	//判断是否需要提权
	if (user_root_auth) {
		ROOT_PRINTF("[+] start get root:%s\n", str_root_key);
		//提权ROOT
		ptrace_writedata(target_pid, map_base, (uint8_t*)str_root_key, strlen(str_root_key) + 1);
		parameters[0] = (unsigned long)map_base;
		parameters[1] = 0;
		parameters[2] = 0;
		if (ptrace_call_wrapper(target_pid, "execve", (void*)execve_addr, parameters, 3, &regs) == -1) {
			goto _recovery;
		}
		ROOT_PRINTF("[+] get root finished.\n");
	}

	//判断是否需要改变工作目录
	if (chdir_path && chdir_addr) {
		//写KEY标志进mmap出来的内存
		ptrace_writedata(target_pid, map_base, (uint8_t*)chdir_path, strlen(chdir_path) + 1);
		parameters[0] = (unsigned long)map_base;
		if (ptrace_call_wrapper(target_pid, "chdir", (void*)chdir_addr, parameters, 1, &regs) == -1) {
			goto _recovery;
		}

	}

	//判断是否需要清除环境
	if (clear_env && clearenv_addr) {
		if (ptrace_call_wrapper(target_pid, "clearenv", (void*)clearenv_addr, parameters, 0, &regs) == -1) {
			goto _recovery;
		}
	}

	if (set_env) {
		for (process64_env env_info : *set_env) {
			//写KEY标志进mmap出来的内存
			ptrace_writedata(target_pid, map_base, (uint8_t*)env_info.key, strlen(env_info.key) + 1);

			uint8_t* val_mem_addr = map_base + strlen(env_info.key) + 1;

			//写VAL标志进mmap出来的内存
			ptrace_writedata(target_pid, val_mem_addr, (uint8_t*)env_info.value, strlen(env_info.value) + 1);

			parameters[0] = (unsigned long)map_base;
			parameters[1] = (unsigned long)(map_base + strlen(env_info.key) + 1);
			parameters[2] = 1;
			//执行setenv，等于setenv("XXX", "XXXXX", 1);
			if (ptrace_call_wrapper(target_pid, "setenv", (void*)setenv_addr, parameters, 3, &regs) == -1) {
				goto _recovery;
			}

		}
	}
	//将要注入的cmd命令写入前面mmap出来的内存
	ptrace_writedata(target_pid, map_base, (uint8_t*)cmd, strlen(cmd) + 1);
	ptrace_writedata(target_pid, map_base + strlen(cmd) + 1, (uint8_t*)str_r, strlen(str_r) + 1);
	parameters[0] = (unsigned long)map_base;
	parameters[1] = (unsigned long)(map_base + strlen(cmd) + 1);
	//执行popen(cmd, "r");
	if (ptrace_call_wrapper(target_pid, "popen", (void*)popen_addr, parameters, 2, &regs) == -1) {
		goto _recovery;
	}
	fp_cmd = (FILE *)ptrace_retval(&regs);
	if (!fp_cmd || fp_cmd == (FILE *)-1) {
		//popen error
		ROOT_PRINTF("[+] popen error\n");
		goto _recovery;
	}
	ROOT_PRINTF("[+] popen success: %p\n", fp_cmd);

	parameters[0] = (unsigned long)fp_cmd;
	if (ptrace_call_wrapper(target_pid, "fileno", (void*)fileno_addr, parameters, 1, &regs) == -1) {
		goto _recovery;
	}
	pip_fp = (int)ptrace_retval(&regs);
	ROOT_PRINTF("[+] pip_fp:%d\n", pip_fp);

	// 循环读取内容
	while(true) {
		parameters[0] = pip_fp;
		parameters[1] = (unsigned long)map_base;
		parameters[2] = (unsigned long)remote_alloc_buf_size;
		//执行read(pip_fp, map_base, remote_alloc_buf_size);
		if (ptrace_call_wrapper(target_pid, "read", (void*)read_addr, parameters, 3, &regs) == -1) {
			goto _recovery;
		}
		remote_really_read = (ssize_t)ptrace_retval(&regs);
		ROOT_PRINTF("[+] remote_really_read: %zd, %p\n", remote_really_read, erron_regs.regs[0]);

		//获取erron
		if (ptrace_getregs(target_pid, &erron_regs) == -1) {
			goto _recovery;
		}

		if (remote_really_read == -1 && erron_regs.regs[0] == EAGAIN) {
			continue; //意味着现在没有可用的数据,以后再试一次
		} else if(remote_really_read > 0) {
			memset(sp_out_shell_buf.get(), 0, remote_alloc_buf_size);
			ptrace_readdata(target_pid, (uint8_t*)parameters[1], (uint8_t*)sp_out_shell_buf.get(), remote_really_read);
			std::string str_convert(sp_out_shell_buf.get(), remote_really_read);
			cmd_exec_result += str_convert;
		} else {
			break;
		}
	}

	ROOT_PRINTF("[+] popen result: %s\n", cmd_exec_result.c_str());

	parameters[0] = (unsigned long)fp_cmd;
	//执行pclose(fp_cmd);
	if (ptrace_call_wrapper(target_pid, "pclose", (void*)pclose_addr, parameters, 1, &regs) == -1) {
		goto _recovery;
	}

	//解除绑定内存
	parameters[0] = (unsigned long)map_base;// addr
	parameters[1] = (unsigned long)(remote_alloc_buf_size); // size

	if (ptrace_call_wrapper(target_pid, "munmap", (void*)munmap_addr, parameters, 2, &regs) == -1) {
		goto _recovery;
	}

	out_err = 0;
	//ROOT_PRINTF("Press enter to detach\n");
	//getchar();

	/* restore */
	//⑪恢复现场并退出ptrace:  
_recovery:	ptrace_setregs(target_pid, &original_regs);
_deatch:ptrace_detach(target_pid);

_ret:	return cmd_exec_result;
}




ssize_t inject_process_env64_PATH(
	int target_pid,
	const char* libc64_so_path,
	size_t& p_mmap_offset,
	size_t& p_munmap_offset,
	size_t& p_getenv_offset,
	size_t& p_setenv_offset,
	const char* add_path) {
	size_t input_env_buf_size = 0;
	ssize_t ret = -231;
	size_t remote_libc64_handle = 0;
	size_t mmap_addr, munmap_addr, getenv_addr, setenv_addr;
	uint8_t* map_base;

	struct pt_regs regs, original_regs;
	unsigned long parameters[10];
	const char* str_flag_path = "PATH";
	char* ret_getenv = NULL;
	size_t tmp_read_byte_index = 0;
	char tmp_read_byte[2] = { 0 };
	std::string str_cur_path;
	//将要注入的cmd命令写入前面mmap出来的内存

	input_env_buf_size = (strlen(add_path) + 1 / getpagesize()) * getpagesize();
	if((strlen(add_path) + 1) % getpagesize()) {
		input_env_buf_size += getpagesize();
	}
	if(input_env_buf_size == 0) {
		input_env_buf_size = getpagesize();
	}

	ROOT_PRINTF("[+] Injecting process: %d\n", target_pid);

	//①ATTATCH，指定目标进程，开始调试  
	if (ptrace_attach(target_pid) == -1) {
		goto _ret;
	}

	//②GETREGS，获取目标进程的寄存器，保存现场  
	if (ptrace_getregs(target_pid, &regs) == -1) {
		goto _deatch;
	}

	/* save original registers */
	memcpy(&original_regs, &regs, sizeof(regs));

	//③获取目的进程的mmap函数的地址，以便为libxxx.so分配内存  

	/*
		需要对(void*)mmap进行说明：这是取得inject本身进程的mmap函数的地址，由于mmap函数在libc.so
		库中，为了将libxxx.so加载到目的进程中，就需要使用目的进程的mmap函数，所以需要查找到libc.so库在目的进程的起始地址。
	*/


	//获取远程pid的某个模块的起始地址  
	remote_libc64_handle = (size_t)get_module_base(target_pid, libc64_so_path);
	if (remote_libc64_handle == 0) {
		ROOT_PRINTF("[+] get_module_base failed.\n");
		goto _deatch;
	}
	mmap_addr = p_mmap_offset ? remote_libc64_handle + p_mmap_offset : 0;
	munmap_addr = p_munmap_offset ? remote_libc64_handle + p_munmap_offset : 0;
	getenv_addr = p_getenv_offset ? remote_libc64_handle + p_getenv_offset : 0;
	setenv_addr = p_setenv_offset ? remote_libc64_handle + p_setenv_offset : 0;

	ROOT_PRINTF("[+] Remote mmap address: %p\n", (void*)mmap_addr);
	ROOT_PRINTF("[+] Remote munmap address: %p\n", (void*)munmap_addr);
	ROOT_PRINTF("[+] Remote getenv address: %p\n", (void*)getenv_addr);
	ROOT_PRINTF("[+] Remote setenv address: %p\n", (void*)setenv_addr);

	/* call mmap (null, 0x4000, PROT_READ | PROT_WRITE | PROT_EXEC,
							 MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	匿名申请一块0x4000大小的内存
	*/
	parameters[0] = 0;  // addr      
	parameters[1] = (unsigned long)(input_env_buf_size); // size
	parameters[2] = PROT_READ | PROT_WRITE | PROT_EXEC;  // prot      
	parameters[3] = MAP_ANONYMOUS | MAP_PRIVATE; // flags      
	parameters[4] = 0; //fd      
	parameters[5] = 0; //offset      

	if (ptrace_call_wrapper(target_pid, "mmap", (void*)mmap_addr, parameters, 6, &regs) == -1) {
		goto _recovery;
	}

	//⑤从寄存器中获取mmap函数的返回值，即申请的内存首地址：  
	map_base = (uint8_t*)ptrace_retval(&regs);

	//写PATH标志进mmap出来的内存
	ptrace_writedata(target_pid, map_base, (uint8_t*)str_flag_path, strlen(str_flag_path) + 1);


	parameters[0] = (unsigned long)map_base;
	//执行getenv，等于getenv("PATH");
	if (ptrace_call_wrapper(target_pid, "getenv", (void*)getenv_addr, parameters, 1, &regs) == -1) {
		goto _recovery;
	}
	ret_getenv = (char*)ptrace_retval(&regs);
	if (!ret_getenv) {
		//getenv error
		ROOT_PRINTF("getenv error\n");
		goto _recovery;
	}
	str_cur_path += add_path;
	str_cur_path += ":";
	do {
		tmp_read_byte[0] = '\x00';
		ptrace_readdata(target_pid, (uint8_t*)((size_t)ret_getenv + tmp_read_byte_index), (uint8_t*)&tmp_read_byte, 1);

		tmp_read_byte_index++;
		str_cur_path += tmp_read_byte;
	} while (tmp_read_byte[0] != '\x00');


	ROOT_PRINTF("[+] Remote cur path: %s\n", str_cur_path.c_str());

	//写PATH变量进mmap出来的内存
	ptrace_writedata(target_pid, map_base + strlen(str_flag_path) + 1, (uint8_t*)str_cur_path.c_str(), str_cur_path.length() + 1);


	parameters[0] = (unsigned long)map_base;
	parameters[1] = (unsigned long)(map_base + strlen(str_flag_path) + 1);
	parameters[2] = 1;
	//执行setenv，等于setenv("PATH", "XXXXX", 1);
	if (ptrace_call_wrapper(target_pid, "setenv", (void*)setenv_addr, parameters, 3, &regs) == -1) {
		goto _recovery;
	}
	if (ptrace_retval(&regs)) {
		//setenv error
		ROOT_PRINTF("setenv error\n");
		goto _recovery;
	}

	//解除绑定内存（不知道为什么解除内存绑定会导致对方程序crash）
	parameters[0] = (unsigned long)map_base;// addr
	parameters[1] = (unsigned long)(input_env_buf_size); // size

	if (ptrace_call_wrapper(target_pid, "munmap", (void*)munmap_addr, parameters, 2, &regs) == -1) {
		goto _recovery;
	}

	ret = 0;
	//ROOT_PRINTF("Press enter to detach\n");
	//getchar();

	/* restore */
	//⑪恢复现场并退出ptrace:  
_recovery:	ptrace_setregs(target_pid, &original_regs);
_deatch:ptrace_detach(target_pid);

_ret:	return ret;
}





//远程注入so
ssize_t inject_process64_so(
	pid_t target_pid,
	const char* libc64_so_path,
	size_t& p_dlopen_offset,
	size_t& p_dlsym_offset,
	size_t& p_mmap_offset,
	size_t& p_munmap_offset,
	const char* target_so_path,
	const char* target_so_fun_name) {
	size_t target_so_path_len = strlen(target_so_path) + 1;
	size_t target_so_fun_name_len = strlen(target_so_fun_name) + 1;
	size_t input_shell_buf_size = getpagesize();
	ssize_t ret = -232;
	size_t remote_libc64_handle = 0;
	size_t dlopen_addr, dlsym_addr, mmap_addr, munmap_addr;
	uint8_t* map_base;
	void* target_so_handle = NULL;
	void* target_func_addr = NULL;
	struct pt_regs regs, original_regs;
	unsigned long parameters[10];
	int flags;

	if (target_so_path_len > input_shell_buf_size || target_so_fun_name_len > input_shell_buf_size) { //输入太长了
		goto _ret;
	}

	ROOT_PRINTF("[+] Injecting process: %d\n", target_pid);

	//①ATTATCH，指定目标进程，开始调试  
	if (ptrace_attach(target_pid) == -1) {
		goto _ret;
	}

	//②GETREGS，获取目标进程的寄存器，保存现场  
	if (ptrace_getregs(target_pid, &regs) == -1) {
		goto _deatch;
	}

	/* save original registers */
	memcpy(&original_regs, &regs, sizeof(regs));

	//③获取目的进程的mmap函数的地址，以便为libxxx.so分配内存  

	/*
		需要对(void*)mmap进行说明：这是取得inject本身进程的mmap函数的地址，由于mmap函数在libc.so
		库中，为了将libxxx.so加载到目的进程中，就需要使用目的进程的mmap函数，所以需要查找到libc.so库在目的进程的起始地址。
	*/

	//获取远程pid的某个模块的起始地址  
	remote_libc64_handle = (size_t)get_module_base(target_pid, libc64_so_path);
	if (remote_libc64_handle == 0) {
		ROOT_PRINTF("[+] get_module_base failed.\n");
		goto _deatch;
	}
	dlopen_addr = p_dlopen_offset ? remote_libc64_handle + p_dlopen_offset : 0;
	dlsym_addr = p_dlsym_offset ? remote_libc64_handle + p_dlsym_offset : 0;
	mmap_addr = p_mmap_offset ? remote_libc64_handle + p_mmap_offset : 0;
	munmap_addr = p_munmap_offset ? remote_libc64_handle + p_munmap_offset : 0;

	ROOT_PRINTF("[+] Remote dlopen address: %p\n", (void*)p_dlopen_offset);
	ROOT_PRINTF("[+] Remote dlsym address: %p\n", (void*)p_dlsym_offset);
	ROOT_PRINTF("[+] Remote mmap address: %p\n", (void*)mmap_addr);
	ROOT_PRINTF("[+] Remote munmap address: %p\n", (void*)munmap_addr);

	/* call mmap (null, 0x4000, PROT_READ | PROT_WRITE | PROT_EXEC,
							 MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	匿名申请一块0x4000大小的内存
	*/
	parameters[0] = 0;  // addr      
	parameters[1] = (unsigned long)(input_shell_buf_size); // size
	parameters[2] = PROT_READ | PROT_WRITE | PROT_EXEC;  // prot      
	parameters[3] = MAP_ANONYMOUS | MAP_PRIVATE; // flags      
	parameters[4] = 0; //fd      
	parameters[5] = 0; //offset      

	if (ptrace_call_wrapper(target_pid, "mmap", (void*)mmap_addr, parameters, 6, &regs) == -1) {
		goto _recovery;
	}

	//⑤从寄存器中获取mmap函数的返回值，即申请的内存首地址：  
	map_base = (uint8_t*)ptrace_retval(&regs);

	//将要注入的so路径写入前面mmap出来的内存
	ptrace_writedata(target_pid, map_base, (uint8_t*)target_so_path, target_so_path_len);


	parameters[0] = (unsigned long)map_base;
	parameters[1] = (unsigned long)(RTLD_NOW | RTLD_GLOBAL);
	//执行dlopen，等于 target_so_handle = dlopen("xxxxxxx.so", RTLD_NOW | RTLD_GLOBAL);
	if (ptrace_call_wrapper(target_pid, "dlopen", (void*)dlopen_addr, parameters, 2, &regs) == -1) {
		goto _recovery;
	}
	target_so_handle = (void*)ptrace_retval(&regs);
	if (!target_so_handle) {
		//dlopen error
		ROOT_PRINTF("dlopen error\n");
		goto _recovery;
	}

	//将要注入的func名字写入前面mmap出来的内存
	ptrace_writedata(target_pid, map_base, (uint8_t*)target_so_fun_name, target_so_fun_name_len);

	parameters[0] = (unsigned long)target_so_handle;
	parameters[1] = (unsigned long)map_base;
	//执行target_func_addr = dlsym(target_so_handle, "xxxxxxxx");
	if (ptrace_call_wrapper(target_pid, "dlsym", (void*)dlsym_addr, parameters, 2, &regs) == -1) {
		goto _recovery;
	}

	target_func_addr = (void*)ptrace_retval(&regs);
	if (!target_func_addr) {
		//dlsym error
		ROOT_PRINTF("dlsym error\n");
		goto _recovery;
	}

	if (ptrace_call_wrapper(target_pid, "hook_init", target_func_addr, parameters, 0, &regs) == -1) {
		goto _recovery;
	}


	////解除绑定内存
	//parameters[0] = (unsigned long)map_base;// addr
	//parameters[1] = (unsigned long)input_shell_buf_size; // size

	//if (ptrace_call_wrapper(target_pid, "munmap", (void *)munmap_addr, parameters, 2, &regs) == -1)
	//{
	//	goto _recovery;
	//}
	ret = 0;
	//ROOT_PRINTF("Press enter to detach\n");
	//getchar();

	/* restore */
	//⑪恢复现场并退出ptrace:  
_recovery:	ptrace_setregs(target_pid, &original_regs);
_deatch:ptrace_detach(target_pid);

_ret:	return ret;
}


//远程执行exit
ssize_t inject_process64_run_exit(
	pid_t target_pid,
	const char* libc64_so_path,
	size_t& p_exit_offset) {
	ssize_t ret = -232;
	size_t remote_libc64_handle = 0;
	size_t exit_addr;
	struct pt_regs regs, original_regs;
	unsigned long parameters[1];
	ROOT_PRINTF("[+] Injecting process: %d\n", target_pid);

	//①ATTATCH，指定目标进程，开始调试  
	if (ptrace_attach(target_pid) == -1) {
		goto _ret;
	}

	//②GETREGS，获取目标进程的寄存器，保存现场  
	if (ptrace_getregs(target_pid, &regs) == -1) {
		goto _deatch;
	}

	/* save original registers */
	memcpy(&original_regs, &regs, sizeof(regs));

	//③获取目的进程的mmap函数的地址，以便为libxxx.so分配内存  

	/*
		需要对(void*)mmap进行说明：这是取得inject本身进程的mmap函数的地址，由于mmap函数在libc.so
		库中，为了将libxxx.so加载到目的进程中，就需要使用目的进程的mmap函数，所以需要查找到libc.so库在目的进程的起始地址。
	*/

	//获取远程pid的某个模块的起始地址  
	remote_libc64_handle = (size_t)get_module_base(target_pid, libc64_so_path);
	if (remote_libc64_handle == 0) {
		ROOT_PRINTF("[+] get_module_base failed.\n");
		goto _deatch;
	}
	exit_addr = p_exit_offset ? remote_libc64_handle + p_exit_offset : 0;
	ROOT_PRINTF("[+] Remote exit address: %p\n", (void*)p_exit_offset);

	parameters[0] = 0;  
	if (ptrace_call_wrapper(target_pid, "_exit", (void*)exit_addr, parameters, 1, &regs) == -1) {
		goto _recovery;
	}
	ret = 0;
	//ROOT_PRINTF("Press enter to detach\n");
	//getchar();

	/* restore */
	//⑪恢复现场并退出ptrace:  
_recovery:	ptrace_setregs(target_pid, &original_regs);
_deatch:ptrace_detach(target_pid);

_ret:	return ret;
}




std::string inject_process64_run_cmd_wrapper(
	const char* str_root_key,
	pid_t target_pid,
	const char* cmd,
	ssize_t & out_err,
	bool user_root_auth/* = true*/,
	const char* chdir_path /*= NULL*/,
	bool clear_env/* = false*/,
	std::vector<process64_env>* set_env /* = NULL*/,
	api_offset_read_mode api_mode /* = api_offset_read_mode::all*/) {
	out_err = 0;
	if (target_pid <= 0) {
		out_err = -240;
		return {};
	}
	if (cmd == NULL || strlen(cmd) == 0) { return {}; }

	if (kernel_root::get_root(str_root_key) != 0) {
		out_err = -241;
		return {};
	}

	std::string target_process_libc_so_path = find_process_libc_so_path(target_pid);
	if (target_process_libc_so_path.empty()) {
		out_err = -243;
		return {};
	}
	ROOT_PRINTF("target_process_libc_so_path:%s\n", target_process_libc_so_path.c_str());

	size_t p_mmap_offset;
	size_t p_munmap_offset;
	size_t p_chdir_offset;
	size_t p_clearenv_offset;
	size_t p_setenv_offset;
	size_t p_execve_offset;
	size_t p_fileno_offset;
	size_t p_popen_offset;
	size_t p_pclose_offset;
	size_t p_read_offset;
	int r = _load_libc64_run_cmd_func_addr(
		target_process_libc_so_path.c_str(),
		api_mode,
		p_mmap_offset,
		p_munmap_offset,
		p_chdir_offset,
		p_clearenv_offset,
		p_setenv_offset,
		p_execve_offset,
		p_fileno_offset,
		p_popen_offset,
		p_pclose_offset,
		p_read_offset);

	if (r != 0) {
		ROOT_PRINTF("_load_libc64_run_cmd_func_addr error:%d\n", r);
		out_err = r;
		return {};
	}
	ROOT_PRINTF("p_mmap_offset:%zu\n", p_mmap_offset);
	ROOT_PRINTF("p_munmap_offset:%zu\n", p_munmap_offset);
	ROOT_PRINTF("p_chdir_offset:%zu\n", p_chdir_offset);
	ROOT_PRINTF("p_clearenv_offset:%zu\n", p_clearenv_offset);
	ROOT_PRINTF("p_setenv_offset:%zu\n", p_setenv_offset);
	ROOT_PRINTF("p_execve_offset:%zu\n", p_execve_offset);
	ROOT_PRINTF("p_fileno_offset:%zu\n", p_fileno_offset);
	ROOT_PRINTF("p_popen_offset:%zu\n", p_popen_offset);
	ROOT_PRINTF("p_pclose_offset:%zu\n", p_pclose_offset);
	ROOT_PRINTF("p_read_offset:%zu\n", p_read_offset);
	if(!p_mmap_offset || !p_munmap_offset || !p_chdir_offset || !p_clearenv_offset || !p_setenv_offset
	|| !p_execve_offset || !p_fileno_offset || !p_popen_offset || !p_pclose_offset || !p_read_offset) {
		return {};
	}
	return inject_process64_run_cmd(
		str_root_key,
		target_pid,
		target_process_libc_so_path.c_str(),
		p_mmap_offset,
		p_munmap_offset,
		p_chdir_offset,
		p_clearenv_offset,
		p_setenv_offset,
		p_execve_offset,
		p_fileno_offset,
		p_popen_offset,
		p_pclose_offset,
		p_read_offset,
		cmd, out_err,
		user_root_auth,
		chdir_path,
		clear_env,
		set_env);
}


std::string safe_inject_process64_run_cmd_wrapper(
	const char* str_root_key,
	pid_t target_pid,
	const char* cmd,
	ssize_t & out_err,
	bool user_root_auth/* = true*/,
	const char* chdir_path/* = NULL*/,
	bool clear_env/* = false*/,
	std::vector<process64_env>* set_env /* = NULL*/,
	api_offset_read_mode api_mode /* = api_offset_read_mode::all*/) {
	std::string cmd_exec_result;
	if (target_pid <= 0) {
		out_err = -250;
		return {};
	}
	if (cmd == NULL || strlen(cmd) == 0) { return {}; }

	fork_pipe_info finfo;
	if(fork_pipe_child_process(finfo)) {
		out_err = 0;
		cmd_exec_result = inject_process64_run_cmd_wrapper(
			str_root_key,
			target_pid,
			cmd,
			out_err,
			user_root_auth,
			chdir_path,
			clear_env,
			set_env,
			api_mode);
		write_errcode_from_child(finfo, out_err);
		write_string_from_child(finfo, cmd_exec_result);
		_exit(0);
		return {};
	}
	
	out_err = 0;
	if(!wait_fork_child_process(finfo)) {
		out_err = -258;
	} else {
		if(!read_errcode_from_child(finfo, out_err)) {
			out_err = -259;
		} else if(!read_string_from_child(finfo, cmd_exec_result)) {
			out_err = -260;
		}
	}
	return cmd_exec_result;
}

ssize_t inject_process_env64_PATH_wrapper(const char* str_root_key, int target_pid, const char* add_path,
	api_offset_read_mode api_mode /* = api_offset_read_mode::all*/) {
	if (kernel_root::get_root(str_root_key) != 0) {
		return -271;
	}

	/*
	安卓:
	/apex/com.android.runtime/lib64/bionic/libc.so
	/apex/com.android.runtime/bin/linker64

	Linux进程:
	/system/lib64/libc.so
	/system/bin/linker64

	init进程
	/system/lib64/bootstrap/libc.so
	/system/lib64/bootstrap/linker64
	*/
	std::string target_process_libc_so_path = find_process_libc_so_path(target_pid);
	if (target_process_libc_so_path.empty()) {
		return -273;
	}
	ROOT_PRINTF("target_process_libc_so_path:%s\n", target_process_libc_so_path.c_str());

	size_t p_mmap_offset;
	size_t p_munmap_offset;
	size_t p_getenv_offset;
	size_t p_setenv_offset;
	int ret = _load_libc64_modify_env_func_addr(
		str_root_key,
		target_process_libc_so_path.c_str(),
		api_mode,
		p_mmap_offset,
		p_munmap_offset,
		p_getenv_offset,
		p_setenv_offset);
	if (ret != 0) {
		ROOT_PRINTF("_load_libc64_modify_env_func_addr error:%d\n", ret);
		return ret;
	}
	ROOT_PRINTF("p_mmap_offset:%zu\n", p_mmap_offset);
	ROOT_PRINTF("p_munmap_offset:%zu\n", p_munmap_offset);
	ROOT_PRINTF("p_getenv_offset:%zu\n", p_getenv_offset);
	ROOT_PRINTF("p_setenv_offset:%zu\n", p_setenv_offset);
	if(!p_mmap_offset || !p_munmap_offset || !p_getenv_offset || !p_setenv_offset) {
		return -274;
	}
	if (inject_process_env64_PATH(target_pid, target_process_libc_so_path.c_str(), p_mmap_offset, p_munmap_offset, p_getenv_offset, p_setenv_offset, add_path) != 0) {
		return -275;
	}
	return 0;
}


ssize_t safe_inject_process_env64_PATH_wrapper(const char* str_root_key, int target_pid, const char* add_path,
	api_offset_read_mode api_mode /* = api_offset_read_mode::all*/) {
	ssize_t out_err;
	std::string libc_path;
	fork_pipe_info finfo;
	if(fork_pipe_child_process(finfo)) {
		out_err = 0;
		if (kernel_root::get_root(str_root_key) == 0) {
			libc_path = find_process_libc_so_path(target_pid);
			if (libc_path.empty()) {
				out_err = -282;
			}
		} else {
			out_err = -281;
		}
		write_errcode_from_child(finfo, out_err);
		write_string_from_child(finfo, libc_path);
		_exit(0);
		return {};
	}
	
	out_err = 0;
	if(!wait_fork_child_process(finfo)) {
		out_err = -283;
	} else {
		if(!read_errcode_from_child(finfo, out_err)) {
			out_err = -284;
		} else if(!read_string_from_child(finfo, libc_path)) {
			out_err = -285;
		}
	}
	if(out_err != 0) {
		return out_err;
	} else if(libc_path.empty()) {
		out_err = -286;
		return out_err;
	}
	ROOT_PRINTF("target_process_libc_so_path:%s\n", libc_path.c_str());


	size_t p_mmap_offset;
	size_t p_munmap_offset;
	size_t p_getenv_offset;
	size_t p_setenv_offset;
	out_err = _load_libc64_modify_env_func_addr(
		str_root_key,
		libc_path.c_str(),
		api_mode,
		p_mmap_offset,
		p_munmap_offset,
		p_getenv_offset,
		p_setenv_offset);

	if (out_err != 0) {
		ROOT_PRINTF("_load_libc64_modify_env_func_addr error:%zd\n", out_err);
		return out_err;
	}
	ROOT_PRINTF("p_mmap_offset:%zu\n", p_mmap_offset);
	ROOT_PRINTF("p_munmap_offset:%zu\n", p_munmap_offset);
	ROOT_PRINTF("p_getenv_offset:%zu\n", p_getenv_offset);
	ROOT_PRINTF("p_setenv_offset:%zu\n", p_setenv_offset);

	if(!p_mmap_offset || !p_munmap_offset || !p_getenv_offset || !p_setenv_offset) {
		return -287;
	}

	finfo.reset();
	if(fork_pipe_child_process(finfo)) {
		out_err = 0;
		if (kernel_root::get_root(str_root_key) == 0) {
			out_err = inject_process_env64_PATH(target_pid, libc_path.c_str(), p_mmap_offset, p_munmap_offset, p_getenv_offset, p_setenv_offset, add_path);
		} else {
			out_err = -288;
		}
		write_errcode_from_child(finfo, out_err);
		_exit(0);
		return {};
	}
	
	out_err = 0;
	if(!wait_fork_child_process(finfo)) {
		out_err = -289;
	} else {
		if(!read_errcode_from_child(finfo, out_err)) {
			out_err = -290;
		}
	}
	return out_err;
}


ssize_t kill_process(const char* str_root_key, pid_t pid) {
	ssize_t err = 0;
	if (kernel_root::get_root(str_root_key) == 0) {
		if(kill(pid, SIGKILL) != 0) {
			err = -350;
		}
	} else {
		err = -351;
	}
	return err;
}

ssize_t safe_kill_process(const char* str_root_key, pid_t pid) {


	fork_pipe_info finfo;
	if(fork_pipe_child_process(finfo)) {
		ssize_t err = kill_process(str_root_key, pid);
		write_errcode_from_child(finfo, err);
		_exit(0);
		return 0;
	}
	ssize_t err = 0;
	if(!wait_fork_child_process(finfo)) {
		err = -360;
	} else {
		if(!read_errcode_from_child(finfo, err)) {
			err = -361;
		}
	}
	return err;
}
}
