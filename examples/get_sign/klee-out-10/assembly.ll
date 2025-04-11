; ModuleID = '/home/klee/workdir/examples/get_sign/get_sign.bc'
source_filename = "get_sign.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [2 x i8] c"a\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @get_sign(i32 %x) #0 !dbg !9 {
entry:
  %retval = alloca i32, align 4
  %x.addr = alloca i32, align 4
  store i32 %x, i32* %x.addr, align 4
  call void @llvm.dbg.declare(metadata i32* %x.addr, metadata !13, metadata !DIExpression()), !dbg !14
  %0 = load i32, i32* %x.addr, align 4, !dbg !15
  %cmp = icmp eq i32 %0, 0, !dbg !17
  br i1 %cmp, label %if.then, label %if.end, !dbg !18

if.then:                                          ; preds = %entry
  store i32 0, i32* %retval, align 4, !dbg !19
  br label %return, !dbg !19

if.end:                                           ; preds = %entry
  %1 = load i32, i32* %x.addr, align 4, !dbg !20
  %cmp1 = icmp slt i32 %1, 0, !dbg !22
  br i1 %cmp1, label %if.then2, label %if.else, !dbg !23

if.then2:                                         ; preds = %if.end
  store i32 -1, i32* %retval, align 4, !dbg !24
  br label %return, !dbg !24

if.else:                                          ; preds = %if.end
  store i32 1, i32* %retval, align 4, !dbg !25
  br label %return, !dbg !25

return:                                           ; preds = %if.else, %if.then2, %if.then
  %2 = load i32, i32* %retval, align 4, !dbg !26
  ret i32 %2, !dbg !26
}

; Function Attrs: nofree nosync nounwind readnone speculatable willreturn
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 !dbg !27 {
entry:
  %retval = alloca i32, align 4
  %a = alloca i32, align 4
  store i32 0, i32* %retval, align 4
  call void @llvm.dbg.declare(metadata i32* %a, metadata !30, metadata !DIExpression()), !dbg !31
  %0 = bitcast i32* %a to i8*, !dbg !32
  call void @klee_make_symbolic(i8* %0, i64 4, i8* getelementptr inbounds ([2 x i8], [2 x i8]* @.str, i64 0, i64 0)), !dbg !33
  %1 = load i32, i32* %a, align 4, !dbg !34
  %call = call i32 @get_sign(i32 %1), !dbg !35
  ret i32 %call, !dbg !36
}

declare dso_local void @klee_make_symbolic(i8*, i64, i8*) #2

attributes #0 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nofree nosync nounwind readnone speculatable willreturn }
attributes #2 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5, !6, !7}
!llvm.ident = !{!8}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 13.0.1 (https://github.com/llvm/llvm-project.git 75e33f71c2dae584b13a7d1186ae0a038ba98838)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "get_sign.c", directory: "/home/klee/workdir/examples/get_sign")
!2 = !{}
!3 = !{i32 7, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{i32 7, !"uwtable", i32 1}
!7 = !{i32 7, !"frame-pointer", i32 2}
!8 = !{!"clang version 13.0.1 (https://github.com/llvm/llvm-project.git 75e33f71c2dae584b13a7d1186ae0a038ba98838)"}
!9 = distinct !DISubprogram(name: "get_sign", scope: !1, file: !1, line: 7, type: !10, scopeLine: 7, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!10 = !DISubroutineType(types: !11)
!11 = !{!12, !12}
!12 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!13 = !DILocalVariable(name: "x", arg: 1, scope: !9, file: !1, line: 7, type: !12)
!14 = !DILocation(line: 7, column: 18, scope: !9)
!15 = !DILocation(line: 8, column: 7, scope: !16)
!16 = distinct !DILexicalBlock(scope: !9, file: !1, line: 8, column: 7)
!17 = !DILocation(line: 8, column: 9, scope: !16)
!18 = !DILocation(line: 8, column: 7, scope: !9)
!19 = !DILocation(line: 9, column: 6, scope: !16)
!20 = !DILocation(line: 11, column: 7, scope: !21)
!21 = distinct !DILexicalBlock(scope: !9, file: !1, line: 11, column: 7)
!22 = !DILocation(line: 11, column: 9, scope: !21)
!23 = !DILocation(line: 11, column: 7, scope: !9)
!24 = !DILocation(line: 12, column: 6, scope: !21)
!25 = !DILocation(line: 14, column: 6, scope: !21)
!26 = !DILocation(line: 15, column: 1, scope: !9)
!27 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 17, type: !28, scopeLine: 17, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!28 = !DISubroutineType(types: !29)
!29 = !{!12}
!30 = !DILocalVariable(name: "a", scope: !27, file: !1, line: 18, type: !12)
!31 = !DILocation(line: 18, column: 7, scope: !27)
!32 = !DILocation(line: 19, column: 22, scope: !27)
!33 = !DILocation(line: 19, column: 3, scope: !27)
!34 = !DILocation(line: 20, column: 19, scope: !27)
!35 = !DILocation(line: 20, column: 10, scope: !27)
!36 = !DILocation(line: 20, column: 3, scope: !27)
