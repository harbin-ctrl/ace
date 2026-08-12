#ifndef AMIGA_SHELL_NATIVE_SHCOMMANDS_H
#define AMIGA_SHELL_NATIVE_SHCOMMANDS_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <exec/types.h>

struct ExecBase;
struct DosLibrary;
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;

typedef struct NativeArgSpec {
    const char *type_name;
    const char *name;
    const char *modifier;
    size_t type_size;
    void *destination;
} NativeArgSpec;

int native_parse_args(int argc, char **argv, NativeArgSpec *specs,
                      size_t spec_count, const char *template);
void native_free_args(NativeArgSpec *specs, size_t spec_count);
void native_publish_result(int result_code);

#define NATIVE_ARG_DECL(x) NATIVE_ARG_DECL_I x
#define NATIVE_ARG_DECL_I(type, abbr, name, modifier, def) \
    type native_arg_##name;

#define NATIVE_ARG_SPEC(x) NATIVE_ARG_SPEC_I x
#define NATIVE_ARG_SPEC_I(type, abbr, name, modifier, def) \
    { #type, #name, #modifier, sizeof(type), &native_arg_##name }

#define NATIVE_ARG_DEFAULT(x) NATIVE_ARG_DEFAULT_I x
#define NATIVE_ARG_DEFAULT_I(type, abbr, name, modifier, def) \
    native_arg_##name = (type)(uintptr_t)(def);

#define NATIVE_ARG_TEMPLATE(x) NATIVE_ARG_TEMPLATE_I x
#define NATIVE_ARG_TEMPLATE_I(type, abbr, name, modifier, def) \
    #abbr #name #modifier

#define SHArg(name) native_arg_##name
#define SHArgLine() native_arg_line

#define AROS_SHA(type, abbr, name, modifier, def) \
    (type, abbr, name, modifier, def)

#define AROS_SHAH(type, abbr, name, modifier, def, help) \
    (type, abbr, name, modifier, def)

#define AROS_SHCOMMAND_INIT
#define AROS_SHCOMMAND_EXIT \
    } } } \
    int main(int argc, char **argv) { \
        int native_result = native_command_main(argc, argv); \
        native_publish_result(native_result); \
        return native_result;

#define NATIVE_SH_ARGS(name, count, ...) \
    static int native_command_main(int argc, char **argv) { \
        __VA_ARGS__ \
        NativeArgSpec native_specs[] = { __VA_ARGS__ }; \
        (void)native_specs;

#define AROS_SH0(name, version) \
    static int native_command_main(int argc, char **argv) { \
        (void)argc; (void)argv; {

#define AROS_SH1(name, version, a1) \
    static int native_command_main(int argc, char **argv) { \
        NATIVE_ARG_DECL(a1) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1) }; \
        NATIVE_ARG_DEFAULT(a1) \
        if (native_parse_args(argc, argv, native_specs, 1, NATIVE_ARG_TEMPLATE(a1)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH1H(name, version, help, a1) \
    static int native_command_main(int argc, char **argv) { \
        (void)(help); \
        NATIVE_ARG_DECL(a1) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1) }; \
        NATIVE_ARG_DEFAULT(a1) \
        if (native_parse_args(argc, argv, native_specs, 1, NATIVE_ARG_TEMPLATE(a1)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH2H(name, version, help, a1, a2) \
    static int native_command_main(int argc, char **argv) { \
        (void)(help); \
        NATIVE_ARG_DECL(a1) NATIVE_ARG_DECL(a2) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1), NATIVE_ARG_SPEC(a2) }; \
        NATIVE_ARG_DEFAULT(a1) NATIVE_ARG_DEFAULT(a2) \
        if (native_parse_args(argc, argv, native_specs, 2, NATIVE_ARG_TEMPLATE(a1) "," NATIVE_ARG_TEMPLATE(a2)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH2(name, version, a1, a2) \
    static int native_command_main(int argc, char **argv) { \
        NATIVE_ARG_DECL(a1) NATIVE_ARG_DECL(a2) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1), NATIVE_ARG_SPEC(a2) }; \
        NATIVE_ARG_DEFAULT(a1) NATIVE_ARG_DEFAULT(a2) \
        if (native_parse_args(argc, argv, native_specs, 2, NATIVE_ARG_TEMPLATE(a1) "," NATIVE_ARG_TEMPLATE(a2)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH3(name, version, a1, a2, a3) \
    static int native_command_main(int argc, char **argv) { \
        NATIVE_ARG_DECL(a1) NATIVE_ARG_DECL(a2) NATIVE_ARG_DECL(a3) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1), NATIVE_ARG_SPEC(a2), NATIVE_ARG_SPEC(a3) }; \
        NATIVE_ARG_DEFAULT(a1) NATIVE_ARG_DEFAULT(a2) NATIVE_ARG_DEFAULT(a3) \
        if (native_parse_args(argc, argv, native_specs, 3, NATIVE_ARG_TEMPLATE(a1) "," NATIVE_ARG_TEMPLATE(a2) "," NATIVE_ARG_TEMPLATE(a3)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH4(name, version, a1, a2, a3, a4) \
    static int native_command_main(int argc, char **argv) { \
        NATIVE_ARG_DECL(a1) NATIVE_ARG_DECL(a2) NATIVE_ARG_DECL(a3) NATIVE_ARG_DECL(a4) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1), NATIVE_ARG_SPEC(a2), NATIVE_ARG_SPEC(a3), NATIVE_ARG_SPEC(a4) }; \
        NATIVE_ARG_DEFAULT(a1) NATIVE_ARG_DEFAULT(a2) NATIVE_ARG_DEFAULT(a3) NATIVE_ARG_DEFAULT(a4) \
        if (native_parse_args(argc, argv, native_specs, 4, NATIVE_ARG_TEMPLATE(a1) "," NATIVE_ARG_TEMPLATE(a2) "," NATIVE_ARG_TEMPLATE(a3) "," NATIVE_ARG_TEMPLATE(a4)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH5(name, version, a1, a2, a3, a4, a5) \
    static int native_command_main(int argc, char **argv) { \
        NATIVE_ARG_DECL(a1) NATIVE_ARG_DECL(a2) NATIVE_ARG_DECL(a3) NATIVE_ARG_DECL(a4) NATIVE_ARG_DECL(a5) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1), NATIVE_ARG_SPEC(a2), NATIVE_ARG_SPEC(a3), NATIVE_ARG_SPEC(a4), NATIVE_ARG_SPEC(a5) }; \
        NATIVE_ARG_DEFAULT(a1) NATIVE_ARG_DEFAULT(a2) NATIVE_ARG_DEFAULT(a3) NATIVE_ARG_DEFAULT(a4) NATIVE_ARG_DEFAULT(a5) \
        if (native_parse_args(argc, argv, native_specs, 5, NATIVE_ARG_TEMPLATE(a1) "," NATIVE_ARG_TEMPLATE(a2) "," NATIVE_ARG_TEMPLATE(a3) "," NATIVE_ARG_TEMPLATE(a4) "," NATIVE_ARG_TEMPLATE(a5)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH6(name, version, a1, a2, a3, a4, a5, a6) \
    static int native_command_main(int argc, char **argv) { \
        NATIVE_ARG_DECL(a1) NATIVE_ARG_DECL(a2) NATIVE_ARG_DECL(a3) NATIVE_ARG_DECL(a4) NATIVE_ARG_DECL(a5) NATIVE_ARG_DECL(a6) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1), NATIVE_ARG_SPEC(a2), NATIVE_ARG_SPEC(a3), NATIVE_ARG_SPEC(a4), NATIVE_ARG_SPEC(a5), NATIVE_ARG_SPEC(a6) }; \
        NATIVE_ARG_DEFAULT(a1) NATIVE_ARG_DEFAULT(a2) NATIVE_ARG_DEFAULT(a3) NATIVE_ARG_DEFAULT(a4) NATIVE_ARG_DEFAULT(a5) NATIVE_ARG_DEFAULT(a6) \
        if (native_parse_args(argc, argv, native_specs, 6, NATIVE_ARG_TEMPLATE(a1) "," NATIVE_ARG_TEMPLATE(a2) "," NATIVE_ARG_TEMPLATE(a3) "," NATIVE_ARG_TEMPLATE(a4) "," NATIVE_ARG_TEMPLATE(a5) "," NATIVE_ARG_TEMPLATE(a6)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH7(name, version, a1, a2, a3, a4, a5, a6, a7) \
    static int native_command_main(int argc, char **argv) { \
        NATIVE_ARG_DECL(a1) NATIVE_ARG_DECL(a2) NATIVE_ARG_DECL(a3) NATIVE_ARG_DECL(a4) NATIVE_ARG_DECL(a5) NATIVE_ARG_DECL(a6) NATIVE_ARG_DECL(a7) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1), NATIVE_ARG_SPEC(a2), NATIVE_ARG_SPEC(a3), NATIVE_ARG_SPEC(a4), NATIVE_ARG_SPEC(a5), NATIVE_ARG_SPEC(a6), NATIVE_ARG_SPEC(a7) }; \
        NATIVE_ARG_DEFAULT(a1) NATIVE_ARG_DEFAULT(a2) NATIVE_ARG_DEFAULT(a3) NATIVE_ARG_DEFAULT(a4) NATIVE_ARG_DEFAULT(a5) NATIVE_ARG_DEFAULT(a6) NATIVE_ARG_DEFAULT(a7) \
        if (native_parse_args(argc, argv, native_specs, 7, NATIVE_ARG_TEMPLATE(a1) "," NATIVE_ARG_TEMPLATE(a2) "," NATIVE_ARG_TEMPLATE(a3) "," NATIVE_ARG_TEMPLATE(a4) "," NATIVE_ARG_TEMPLATE(a5) "," NATIVE_ARG_TEMPLATE(a6) "," NATIVE_ARG_TEMPLATE(a7)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH4H(name, version, help, a1, a2, a3, a4) \
    static int native_command_main(int argc, char **argv) { \
        (void)(help); \
        NATIVE_ARG_DECL(a1) NATIVE_ARG_DECL(a2) NATIVE_ARG_DECL(a3) NATIVE_ARG_DECL(a4) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1), NATIVE_ARG_SPEC(a2), NATIVE_ARG_SPEC(a3), NATIVE_ARG_SPEC(a4) }; \
        NATIVE_ARG_DEFAULT(a1) NATIVE_ARG_DEFAULT(a2) NATIVE_ARG_DEFAULT(a3) NATIVE_ARG_DEFAULT(a4) \
        if (native_parse_args(argc, argv, native_specs, 4, NATIVE_ARG_TEMPLATE(a1) "," NATIVE_ARG_TEMPLATE(a2) "," NATIVE_ARG_TEMPLATE(a3) "," NATIVE_ARG_TEMPLATE(a4)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH10(name, version, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
    static int native_command_main(int argc, char **argv) { \
        NATIVE_ARG_DECL(a1) NATIVE_ARG_DECL(a2) NATIVE_ARG_DECL(a3) NATIVE_ARG_DECL(a4) NATIVE_ARG_DECL(a5) NATIVE_ARG_DECL(a6) NATIVE_ARG_DECL(a7) NATIVE_ARG_DECL(a8) NATIVE_ARG_DECL(a9) NATIVE_ARG_DECL(a10) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1), NATIVE_ARG_SPEC(a2), NATIVE_ARG_SPEC(a3), NATIVE_ARG_SPEC(a4), NATIVE_ARG_SPEC(a5), NATIVE_ARG_SPEC(a6), NATIVE_ARG_SPEC(a7), NATIVE_ARG_SPEC(a8), NATIVE_ARG_SPEC(a9), NATIVE_ARG_SPEC(a10) }; \
        NATIVE_ARG_DEFAULT(a1) NATIVE_ARG_DEFAULT(a2) NATIVE_ARG_DEFAULT(a3) NATIVE_ARG_DEFAULT(a4) NATIVE_ARG_DEFAULT(a5) NATIVE_ARG_DEFAULT(a6) NATIVE_ARG_DEFAULT(a7) NATIVE_ARG_DEFAULT(a8) NATIVE_ARG_DEFAULT(a9) NATIVE_ARG_DEFAULT(a10) \
        if (native_parse_args(argc, argv, native_specs, 10, NATIVE_ARG_TEMPLATE(a1) "," NATIVE_ARG_TEMPLATE(a2) "," NATIVE_ARG_TEMPLATE(a3) "," NATIVE_ARG_TEMPLATE(a4) "," NATIVE_ARG_TEMPLATE(a5) "," NATIVE_ARG_TEMPLATE(a6) "," NATIVE_ARG_TEMPLATE(a7) "," NATIVE_ARG_TEMPLATE(a8) "," NATIVE_ARG_TEMPLATE(a9) "," NATIVE_ARG_TEMPLATE(a10)) != 0) return RETURN_ERROR; \
        {

#define AROS_SH3H(name, version, help, a1, a2, a3) \
    static int native_command_main(int argc, char **argv) { \
        (void)(help); \
        NATIVE_ARG_DECL(a1) NATIVE_ARG_DECL(a2) NATIVE_ARG_DECL(a3) \
        NativeArgSpec native_specs[] = { NATIVE_ARG_SPEC(a1), NATIVE_ARG_SPEC(a2), NATIVE_ARG_SPEC(a3) }; \
        NATIVE_ARG_DEFAULT(a1) NATIVE_ARG_DEFAULT(a2) NATIVE_ARG_DEFAULT(a3) \
        if (native_parse_args(argc, argv, native_specs, 3, NATIVE_ARG_TEMPLATE(a1) "," NATIVE_ARG_TEMPLATE(a2) "," NATIVE_ARG_TEMPLATE(a3)) != 0) return RETURN_ERROR; \
        {

#endif
