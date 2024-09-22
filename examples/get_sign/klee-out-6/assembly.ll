; ModuleID = '/home/klee/workdir/examples/get_sign/test.bc'
source_filename = "test.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [3 x i8] c"y1\00", align 1
@.str.1 = private unnamed_addr constant [3 x i8] c"y2\00", align 1
@.str.2 = private unnamed_addr constant [8 x i8] c"z == 0\0A\00", align 1
@.str.3 = private unnamed_addr constant [36 x i8] c"z == 0 and y1 == 100 and y2 == 200\0A\00", align 1
@.str.4 = private unnamed_addr constant [8 x i8] c"z != 0\0A\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 !dbg !9 {
entry:
  %retval = alloca i32, align 4
  %y1 = alloca i32, align 4
  %y2 = alloca i32, align 4
  %z = alloca i32, align 4
  store i32 0, i32* %retval, align 4
  call void @llvm.dbg.declare(metadata i32* %y1, metadata !13, metadata !DIExpression()), !dbg !14
  %0 = bitcast i32* %y1 to i8*, !dbg !15
  call void @klee_make_symbolic(i8* %0, i64 4, i8* getelementptr inbounds ([3 x i8], [3 x i8]* @.str, i64 0, i64 0)), !dbg !16
  call void @llvm.dbg.declare(metadata i32* %y2, metadata !17, metadata !DIExpression()), !dbg !18
  %1 = bitcast i32* %y2 to i8*, !dbg !19
  call void @klee_make_symbolic(i8* %1, i64 4, i8* getelementptr inbounds ([3 x i8], [3 x i8]* @.str.1, i64 0, i64 0)), !dbg !20
  call void @llvm.dbg.declare(metadata i32* %z, metadata !21, metadata !DIExpression()), !dbg !22
  %2 = load i32, i32* %y1, align 4, !dbg !23
  %3 = load i32, i32* %y2, align 4, !dbg !24
  %add = add nsw i32 %2, %3, !dbg !25
  store i32 %add, i32* %z, align 4, !dbg !22
  %4 = load i32, i32* %z, align 4, !dbg !26
  %cmp = icmp eq i32 %4, 0, !dbg !28
  br i1 %cmp, label %if.then, label %if.else, !dbg !29

if.then:                                          ; preds = %entry
  %call = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([8 x i8], [8 x i8]* @.str.2, i64 0, i64 0)), !dbg !30
  %5 = load i32, i32* %y1, align 4, !dbg !32
  %cmp1 = icmp eq i32 %5, 100, !dbg !34
  br i1 %cmp1, label %land.lhs.true, label %if.end, !dbg !35

land.lhs.true:                                    ; preds = %if.then
  %6 = load i32, i32* %y2, align 4, !dbg !36
  %cmp2 = icmp eq i32 %6, 200, !dbg !37
  br i1 %cmp2, label %if.then3, label %if.end, !dbg !38

if.then3:                                         ; preds = %land.lhs.true
  %call4 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([36 x i8], [36 x i8]* @.str.3, i64 0, i64 0)), !dbg !39
  br label %if.end, !dbg !41

if.end:                                           ; preds = %if.then3, %land.lhs.true, %if.then
  br label %if.end6, !dbg !42

if.else:                                          ; preds = %entry
  %call5 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([8 x i8], [8 x i8]* @.str.4, i64 0, i64 0)), !dbg !43
  br label %if.end6

if.end6:                                          ; preds = %if.else, %if.end
  ret i32 0, !dbg !45
}

; Function Attrs: nofree nosync nounwind readnone speculatable willreturn
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

declare dso_local void @klee_make_symbolic(i8*, i64, i8*) #2

declare dso_local i32 @printf(i8*, ...) #2

attributes #0 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nofree nosync nounwind readnone speculatable willreturn }
attributes #2 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5, !6, !7}
!llvm.ident = !{!8}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 13.0.1 (https://github.com/llvm/llvm-project.git 75e33f71c2dae584b13a7d1186ae0a038ba98838)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "test.c", directory: "/home/klee/workdir/examples/get_sign")
!2 = !{}
!3 = !{i32 7, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{i32 7, !"uwtable", i32 1}
!7 = !{i32 7, !"frame-pointer", i32 2}
!8 = !{!"clang version 13.0.1 (https://github.com/llvm/llvm-project.git 75e33f71c2dae584b13a7d1186ae0a038ba98838)"}
!9 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 5, type: !10, scopeLine: 5, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!10 = !DISubroutineType(types: !11)
!11 = !{!12}
!12 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!13 = !DILocalVariable(name: "y1", scope: !9, file: !1, line: 6, type: !12)
!14 = !DILocation(line: 6, column: 7, scope: !9)
!15 = !DILocation(line: 7, column: 22, scope: !9)
!16 = !DILocation(line: 7, column: 3, scope: !9)
!17 = !DILocalVariable(name: "y2", scope: !9, file: !1, line: 8, type: !12)
!18 = !DILocation(line: 8, column: 7, scope: !9)
!19 = !DILocation(line: 9, column: 22, scope: !9)
!20 = !DILocation(line: 9, column: 3, scope: !9)
!21 = !DILocalVariable(name: "z", scope: !9, file: !1, line: 11, type: !12)
!22 = !DILocation(line: 11, column: 7, scope: !9)
!23 = !DILocation(line: 11, column: 12, scope: !9)
!24 = !DILocation(line: 11, column: 17, scope: !9)
!25 = !DILocation(line: 11, column: 15, scope: !9)
!26 = !DILocation(line: 13, column: 7, scope: !27)
!27 = distinct !DILexicalBlock(scope: !9, file: !1, line: 13, column: 7)
!28 = !DILocation(line: 13, column: 9, scope: !27)
!29 = !DILocation(line: 13, column: 7, scope: !9)
!30 = !DILocation(line: 16, column: 5, scope: !31)
!31 = distinct !DILexicalBlock(scope: !27, file: !1, line: 15, column: 3)
!32 = !DILocation(line: 17, column: 9, scope: !33)
!33 = distinct !DILexicalBlock(scope: !31, file: !1, line: 17, column: 9)
!34 = !DILocation(line: 17, column: 12, scope: !33)
!35 = !DILocation(line: 17, column: 19, scope: !33)
!36 = !DILocation(line: 17, column: 22, scope: !33)
!37 = !DILocation(line: 17, column: 25, scope: !33)
!38 = !DILocation(line: 17, column: 9, scope: !31)
!39 = !DILocation(line: 19, column: 7, scope: !40)
!40 = distinct !DILexicalBlock(scope: !33, file: !1, line: 17, column: 33)
!41 = !DILocation(line: 20, column: 5, scope: !40)
!42 = !DILocation(line: 21, column: 3, scope: !31)
!43 = !DILocation(line: 22, column: 5, scope: !44)
!44 = distinct !DILexicalBlock(scope: !27, file: !1, line: 21, column: 10)
!45 = !DILocation(line: 24, column: 3, scope: !9)
