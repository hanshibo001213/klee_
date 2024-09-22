; ModuleID = 'test.bc'
source_filename = "test.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [2 x i8] c"x\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 !dbg !9 {
entry:
  %retval = alloca i32, align 4
  %x = alloca i32, align 4
  store i32 0, i32* %retval, align 4
  call void @llvm.dbg.declare(metadata i32* %x, metadata !13, metadata !DIExpression()), !dbg !14
  store i32 0, i32* %x, align 4, !dbg !14
  %0 = bitcast i32* %x to i8*, !dbg !15
  call void @klee_make_symbolic(i8* %0, i64 4, i8* getelementptr inbounds ([2 x i8], [2 x i8]* @.str, i64 0, i64 0)), !dbg !16
  br label %while.cond, !dbg !17

while.cond:                                       ; preds = %while.body, %entry
  %1 = load i32, i32* %x, align 4, !dbg !18
  %cmp = icmp slt i32 %1, 3, !dbg !19
  br i1 %cmp, label %while.body, label %while.end, !dbg !17

while.body:                                       ; preds = %while.cond
  %2 = load i32, i32* %x, align 4, !dbg !20
  %inc = add nsw i32 %2, 1, !dbg !20
  store i32 %inc, i32* %x, align 4, !dbg !20
  br label %while.cond, !dbg !17, !llvm.loop !22

while.end:                                        ; preds = %while.cond
  ret i32 0, !dbg !25
}

; Function Attrs: nofree nosync nounwind readnone speculatable willreturn
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

declare dso_local void @klee_make_symbolic(i8*, i64, i8*) #2

attributes #0 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nofree nosync nounwind readnone speculatable willreturn }
attributes #2 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5, !6, !7}
!llvm.ident = !{!8}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 13.0.1 (https://github.com/llvm/llvm-project.git 75e33f71c2dae584b13a7d1186ae0a038ba98838)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "test.c", directory: "/home/klee/workdir/examples/test")
!2 = !{}
!3 = !{i32 7, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{i32 7, !"uwtable", i32 1}
!7 = !{i32 7, !"frame-pointer", i32 2}
!8 = !{!"clang version 13.0.1 (https://github.com/llvm/llvm-project.git 75e33f71c2dae584b13a7d1186ae0a038ba98838)"}
!9 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 3, type: !10, scopeLine: 3, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!10 = !DISubroutineType(types: !11)
!11 = !{!12}
!12 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!13 = !DILocalVariable(name: "x", scope: !9, file: !1, line: 4, type: !12)
!14 = !DILocation(line: 4, column: 7, scope: !9)
!15 = !DILocation(line: 6, column: 22, scope: !9)
!16 = !DILocation(line: 6, column: 3, scope: !9)
!17 = !DILocation(line: 7, column: 3, scope: !9)
!18 = !DILocation(line: 7, column: 10, scope: !9)
!19 = !DILocation(line: 7, column: 12, scope: !9)
!20 = !DILocation(line: 8, column: 6, scope: !21)
!21 = distinct !DILexicalBlock(scope: !9, file: !1, line: 7, column: 17)
!22 = distinct !{!22, !17, !23, !24}
!23 = !DILocation(line: 9, column: 3, scope: !9)
!24 = !{!"llvm.loop.mustprogress"}
!25 = !DILocation(line: 11, column: 3, scope: !9)
