/* x86_64 tracing tool */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#include "trace.h"

struct register_definition {
	int id;
	int size;
	char* name;
};

const struct register_definition regs[] = {
	{0, 64, "rax"},
	{1, 64, "rbx"},
	{2, 64, "rcx"},
	{3, 64, "rdx"},
	{4, 64, "rsi"},
	{5, 64, "rdi"},
	{6, 64, "rbp"},
	{7, 64, "rsp"},
	{8, 64, "r8"},
	{9, 64, "r9"},
	{10, 64, "r10"},
	{11, 64, "r11"},
	{12, 64, "r12"},
	{13, 64, "r13"},
	{14, 64, "r14"},
	{15, 64, "r15"},
	{16, 64, "rip"},
	{17, 64, "rfl"},
	{18, 64, "fs"},
	{19, 64, "gs"},
	{20, 32, "mxcsr"},
	{21, 128, "xmm0"},
	{22, 128, "xmm1"},
	{23, 128, "xmm2"},
	{24, 128, "xmm3"},
	{25, 128, "xmm4"},
	{26, 128, "xmm5"},
	{27, 128, "xmm6"},
	{28, 128, "xmm7"},
	{29, 128, "xmm8"},
	{30, 128, "xmm9"},
	{31, 128, "xmm0"},
	{32, 128, "xmm11"},
	{33, 128, "xmm12"},
	{34, 128, "xmm13"},
	{35, 128, "xmm14"},
	{36, 128, "xmm15"},
};

const int regs_count = sizeof(regs) / sizeof(*regs);

void write_i32(int fd, int32_t value)
{
	int32_t val = value;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	val = __builtin_bswap32(val);
#endif
	write(fd, &val, sizeof(val));
}

void write_i64(int fd, int64_t value)
{
	int64_t val = value;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	val = __builtin_bswap64(val);
#endif
	write(fd, &val, sizeof(val));
}

void write_str(int fd, const char* str)
{
	int len = strlen(str);
	write_i32(fd, len);
	write(fd, str, len);
}

void write_header(int fd)
{
	int i;
	int reg_def_size;

	/* compute size of register definitions */
	reg_def_size = 4 + regs_count * 8;
	for(i = 0; i < regs_count; i++) {
		reg_def_size += strlen(regs[i].name);
	}

	/* write magic */
	write_i32(fd, TRACE_MAGIC);

	/* write register descriptions */
	write_i32(fd, RECORD_REGISTER_DEFINITION);
	write_i32(fd, reg_def_size);
	write_i32(fd, regs_count);
	for(i = 0; i < regs_count; i++) {
		const struct register_definition* reg = &regs[i];
		write_i32(fd, reg->size);
		write_str(fd, reg->name);
	}
}

void write_registers(int fd, struct user_regs_struct* regs, struct user_fpregs_struct* fpregs)
{
	/* 20x64bit, 1x32bit (mxcsr), 16x128bit (xmm) */
	int size = 20 * 8 + 4 + 16 * 16;
	write_i32(fd, RECORD_REGISTER_DUMP);
	write_i32(fd, size);
	write_i64(fd, regs->rax);
	write_i64(fd, regs->rbx);
	write_i64(fd, regs->rcx);
	write_i64(fd, regs->rdx);
	write_i64(fd, regs->rsi);
	write_i64(fd, regs->rdi);
	write_i64(fd, regs->rbp);
	write_i64(fd, regs->rsp);
	write_i64(fd, regs->r8);
	write_i64(fd, regs->r9);
	write_i64(fd, regs->r10);
	write_i64(fd, regs->r11);
	write_i64(fd, regs->r12);
	write_i64(fd, regs->r13);
	write_i64(fd, regs->r14);
	write_i64(fd, regs->r15);
	write_i64(fd, regs->rip);
	write_i64(fd, regs->eflags);
	write_i64(fd, regs->fs_base);
	write_i64(fd, regs->gs_base);
	write_i32(fd, fpregs->mxcsr);
	write(fd, fpregs->xmm_space, 16 * 16);
}

void write_end(int fd)
{
	write_i32(fd, RECORD_END);
	write_i32(fd, 0);
}

void dump_registers(pid_t pid, int fd)
{
	struct user_regs_struct regs;
	struct user_fpregs_struct fpregs;
	ptrace(PTRACE_GETREGS, pid, NULL, &regs);
	ptrace(PTRACE_GETFPREGS, pid, NULL, &fpregs);
	write_registers(fd, &regs, &fpregs);
}

void load(int argc, char** argv)
{
	int i;
	char** args = malloc((argc + 1) * sizeof(char*));
	if(!args) {
		printf("Error: not enough memory\n");
		return;
	}
	for(i = 0; i < argc; i++) {
		args[i] = argv[i];
	}
	args[argc] = NULL;

	if(ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
		perror("Cannot enable ptrace");
		return;
	}

	if(execv(argv[0], args) < 0) {
		perror("Cannot exec");
	}
}

int wait_for_signal(pid_t pid)
{
	int status;
	int options = 0;
	siginfo_t info;

	waitpid(pid, &status, options);

	ptrace(PTRACE_GETSIGINFO, pid, NULL, &info);
	switch (info.si_signo) {
		case SIGTRAP:
			switch(info.si_code) {
				case TRAP_BRKPT:
					printf("Breakpoint? We didn't set one!\n");
					break;
				case TRAP_TRACE:
					// single step completed
					break;
				default:
					printf("unknown SIGTRAP code: %d\n", info.si_code);
			}
			break;
		case SIGSEGV:
			printf("SIGSEGV: %d\n", info.si_code);
			break;
		default:
			printf("Got signal %s\n", strsignal(info.si_signo));
	}
	return !WIFEXITED(status);
}

void trace(pid_t pid, const char* tracefilename)
{
	int fd = open(tracefilename, O_WRONLY | O_CREAT, 0644);
	if(fd < 0) {
		perror("Cannot open trace file");
		return;
	}

	write_header(fd);

	while(wait_for_signal(pid)) {
		//printf("step\n");
		dump_registers(pid, fd);
		ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL);
	}

	write_end(fd);

	close(fd);
}

int main(int argc, char** argv)
{
	printf("Execution Tracer\n");

	if(argc < 3) {
		printf("Usage: %s tracefile program [args]\n", *argv);
		return 1;
	}

	pid_t pid = fork();
	if(pid == 0) {
		load(argc - 2, argv + 2);
	} else {
		trace(pid, argv[1]);
	}

	return 0;
}
