//===-- ExecutionTree.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ExecutionTree.h"

#include "ExecutionState.h"

#include "klee/Core/Interpreter.h"
#include "klee/Expr/ExprPPrinter.h"
#include "klee/Module/KInstruction.h"
#include "klee/Support/OptionCategories.h"

#include <bitset>
#include <vector>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <jsoncpp/json/json.h>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include "klee/Module/KModule.h"

using namespace klee;
using namespace llvm;

using namespace std;
using namespace Json;

namespace klee {
llvm::cl::OptionCategory
    ExecTreeCat("Execution tree related options",
                "These options affect the execution tree handling.");
}

namespace {
llvm::cl::opt<bool> CompressExecutionTree(
    "compress-execution-tree",
    llvm::cl::desc("Remove intermediate nodes in the execution "
                   "tree whenever possible (default=false)"),
    llvm::cl::init(false), llvm::cl::cat(ExecTreeCat));

llvm::cl::opt<bool> WriteExecutionTree(
    "write-exec-tree", llvm::cl::init(false),
    llvm::cl::desc("Write execution tree into exec_tree.db (default=false)"),
    llvm::cl::cat(ExecTreeCat));
} // namespace

Json::Value jsonTree;
Json::Value jsonData(Json::objectValue);

Json::Value jsonMemoryObjects(Json::arrayValue);

// ExecutionTreeNode

ExecutionTreeNode::ExecutionTreeNode(ExecutionTreeNode *parent,
                                     ExecutionState *state) noexcept
    : parent{parent}, left{nullptr}, right{nullptr}, state{state} {
  state->executionTreeNode = this;
}

// AnnotatedExecutionTreeNode

AnnotatedExecutionTreeNode::AnnotatedExecutionTreeNode(
    ExecutionTreeNode *parent, ExecutionState *state) noexcept
    : ExecutionTreeNode(parent, state) {
  id = nextID++;
}

// NoopExecutionTree

// void NoopExecutionTree::dump(llvm::raw_ostream &os) noexcept {
void NoopExecutionTree::dump() noexcept {
  // os << "digraph G {\nTreeNotAvailable [shape=box]\n}";
}

// InMemoryExecutionTree

InMemoryExecutionTree::InMemoryExecutionTree(
    ExecutionState &initialState) noexcept {
  root = ExecutionTreeNodePtr(createNode(nullptr, &initialState));
  initialState.executionTreeNode = root.getPointer();
}

ExecutionTreeNode *InMemoryExecutionTree::createNode(ExecutionTreeNode *parent,
                                                     ExecutionState *state) {
  return new ExecutionTreeNode(parent, state);
}

void InMemoryExecutionTree::attach(ExecutionTreeNode *node,
                                   ExecutionState *leftState,
                                   ExecutionState *rightState,
                                   BranchType reason) noexcept {
  assert(node && !node->left.getPointer() && !node->right.getPointer());
  assert(node == rightState->executionTreeNode &&
         "Attach assumes the right state is the current state");
  node->left = ExecutionTreeNodePtr(createNode(node, leftState));
  // The current node inherits the tag
  uint8_t currentNodeTag = root.getInt();
  if (node->parent)
    currentNodeTag = node->parent->left.getPointer() == node
                         ? node->parent->left.getInt()
                         : node->parent->right.getInt();
  node->right =
      ExecutionTreeNodePtr(createNode(node, rightState), currentNodeTag);
  updateBranchingNode(*node, reason);
  node->state = nullptr;
}

void InMemoryExecutionTree::remove(ExecutionTreeNode *n) noexcept {
  assert(!n->left.getPointer() && !n->right.getPointer());
  updateTerminatingNode(*n);
  do {
    ExecutionTreeNode *p = n->parent;
    if (p) {
      if (n == p->left.getPointer()) {
        p->left = ExecutionTreeNodePtr(nullptr);
      } else {
        assert(n == p->right.getPointer());
        p->right = ExecutionTreeNodePtr(nullptr);
      }
    }
    delete n;
    n = p;
  } while (n && !n->left.getPointer() && !n->right.getPointer());

  if (n && CompressExecutionTree) {
    // We are now at a node that has exactly one child; we've just deleted the
    // other one. Eliminate the node and connect its child to the parent
    // directly (if it's not the root).
    ExecutionTreeNodePtr child = n->left.getPointer() ? n->left : n->right;
    ExecutionTreeNode *parent = n->parent;

    child.getPointer()->parent = parent;
    if (!parent) {
      // We are at the root
      root = child;
    } else {
      if (n == parent->left.getPointer()) {
        parent->left = child;
      } else {
        assert(n == parent->right.getPointer());
        parent->right = child;
      }
    }

    delete n;
  }
}

bool isSecondElementInJsonArray(Json::Value &jsonArray, std::string &value) {
  if (jsonArray.isArray() && jsonArray.size() >= 2) {
    Json::Value &secondElement = jsonArray[1];
    if (secondElement.isString() && secondElement.asString() == value) {
      return true;
    }
  }
  return false;
}

void removeExtraSpacesAndNewlines(std::string &str) {
  // 移除换行符
  str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());

  // 将连续的空格替换为一个空格
  bool previousIsSpace = false;
  str.erase(std::remove_if(str.begin(), str.end(),
                           [&](char c) {
                             if (std::isspace(c)) {
                               if (previousIsSpace)
                                 return true;
                               previousIsSpace = true;
                             } else {
                               previousIsSpace = false;
                             }
                             return false;
                           }),
            str.end());
}

// std::string parseExpression(const std::string &expression) {
//   std::regex eqRegex(R"(^\(Eq (\w+) \(ReadLSB w32 0 (\w+)\)\)$)");
//   std::regex noteqRegex(
//       R"(^\(Eq false \(Eq (\w+) \(ReadLSB w32 0 (\w+)\)\)\)$)");
//   std::regex notsltRegex(
//       R"(^\(Eq false \(Slt \(ReadLSB w32 0 (\w+)\) (\d+)\)\)$)");
//   std::regex sltRegex(R"(^\(Slt \(ReadLSB w32 0 (\w+)\) (\d+)\)$)");

//   std::string result = expression;

//   if (std::regex_search(result, eqRegex))
//     result = std::regex_replace(result, eqRegex, "$2==$1");
//   if (std::regex_search(result, noteqRegex))
//     result = std::regex_replace(result, noteqRegex, "$2!=$1");
//   if (std::regex_search(result, notsltRegex))
//     result = std::regex_replace(result, notsltRegex, "$1>=$2");
//   if (std::regex_search(result, sltRegex))
//     result = std::regex_replace(result, sltRegex, "$1<$2");

//   return result;
// }

// 函数：将 SMT 公式 (前缀形式) 转换为一阶逻辑公式 (中缀形式)
std::string parseExpression(const std::string &expression) {
  std::stack<std::string> exprStack;
  std::istringstream stream(expression);
  std::vector<std::string> tokens;
  std::string token;
  bool eqFalsePending = false; // 用于标记是否遇到 "Eq false"

  // 使用正则表达式分割 token，包括括号、操作符和操作数
  std::regex tokenRegex(R"([\(\)]|[^\s\(\)]+)");
  auto tokensBegin =
      std::sregex_iterator(expression.begin(), expression.end(), tokenRegex);
  auto tokensEnd = std::sregex_iterator();

  // 收集所有 token 到 vector 中
  for (std::sregex_iterator i = tokensBegin; i != tokensEnd; ++i) {
    tokens.push_back(i->str());
  }

  // 2. 逆序遍历 token（从右向左解析前缀表达式）
  for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
    token = *it;
    // std::cout<<"此时的token"<<token<<endl;
    if (token == "(" || token == ")") {
      continue;
    } else if (token == "false") {
      eqFalsePending = true; // 标记为 "Eq false" 情况
    } else if (token == "ReadLSB") {
      // ReadLSB 需要后面三个 token: w32, 0, 变量名
      std::string w32 = exprStack.top();
      exprStack.pop();
      std::string zero = exprStack.top();
      exprStack.pop();
      std::string variable = exprStack.top();
      exprStack.pop();
      exprStack.push(variable); // 仅将变量名入栈
    }

    // 关系运算
    else if (token == "Eq") {
      std::string operand1 = exprStack.top();
      exprStack.pop();

      if (eqFalsePending) {
        // 处理 (Eq false (XXX ...)) 形式
        std::regex pattern(
            R"((.*) ([<>=!]+) (.*))"); // 匹配 "a < b", "a == b" 等形式
        std::smatch match;

        if (std::regex_match(operand1, match, pattern)) {
          std::string left = match[1].str();
          std::string op = match[2].str();
          std::string right = match[3].str();

          // 取反逻辑
          std::string negatedOp;
          if (op == "<")
            negatedOp = ">=";
          else if (op == "<=")
            negatedOp = ">";
          else if (op == ">")
            negatedOp = "<=";
          else if (op == ">=")
            negatedOp = "<";
          else if (op == "==")
            negatedOp = "!=";
          else if (op == "!=")
            negatedOp = "==";

          exprStack.push(left + " " + negatedOp + " " + right);
        } else {
          std::string operand2 = exprStack.top();
          exprStack.pop();
          exprStack.push(operand1 + " != " + operand2);
        }
        eqFalsePending = false; // 重置标记
      } else {
        std::string operand2 = exprStack.top();
        exprStack.pop();
        // 普通的相等操作符
        exprStack.push(operand1 + " == " + operand2);
      }
    } else if (token == "Ne") // 不等于
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " != " + operand2);
    } else if (token == "Slt") {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " < " + operand2);
    } else if (token == "Sle") // 小于等于（有符号）
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " <= " + operand2);
    } else if (token == "Sgt") // 大于（有符号）
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " > " + operand2);
    } else if (token == "Sge") // 大于等于（有符号）
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " >= " + operand2);
    } else if (token == "Ult") // 小于（无符号）
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " < " + operand2);
    } else if (token == "Ule") // 小于等于（无符号）
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " <= " + operand2);
    } else if (token == "Ugt") // 大于（无符号）
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " > " + operand2);
    } else if (token == "Uge") // 大于等于（无符号）
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " >= " + operand2);
    }

    // 算术运算
    else if (token == "Add") {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " + " + operand2);
    } else if (token == "Sub") {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " - " + operand2);
    } else if (token == "Mul") {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " * " + operand2);
    } else if (token == "UDiv") // 无符号除法
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " / " + operand2);
    } else if (token == "SDiv") // 有符号除法
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " / " + operand2);
    } else if (token == "URem") // 无符号取模
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " % " + operand2);
    } else if (token == "SRem") // 有符号取模
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " % " + operand2);
    }

    // 位运算
    else if (token == "Not") {
      std::string operand = exprStack.top();
      exprStack.pop();
      exprStack.push("~(" + operand + ")"); // 按位取反
    } else if (token == "And")              // 按位与
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " & " + operand2 + ")");
    } else if (token == "Or") // 按位或
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " | " + operand2 + ")");
    } else if (token == "Xor") // 按位异或
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " ^ " + operand2 + ")");
    } else if (token == "Shl") // 左移
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " << " + operand2 + ")");
    } else if (token == "LShr") // 逻辑右移
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " >> " + operand2 + ")"); // 无符号右移
    } else if (token == "AShr")                                 // 算术右移
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " >> " + operand2 + ")"); // 有符号右移
    }

    else {
      // 普通操作数 (数字、变量名) 直接入栈
      exprStack.push(token);
    }
    // std::cout<<"此时的栈"<<exprStack<<endl;
  }

  return exprStack.empty() ? "" : exprStack.top();
}

// void InMemoryExecutionTree::dump(llvm::raw_ostream &os) noexcept {
//   std::unique_ptr<ExprPPrinter> pp(ExprPPrinter::create(os));
//   pp->setNewline("\\l");
// os << "digraph G {\n"
//    << "\tsize=\"10,7.5\";\n"
//    << "\tratio=fill;\n"
//    << "\trotate=90;\n"
//    << "\tcenter = \"true\";\n"
//    << "\tnode [style=\"filled\",width=.1,height=.1,fontname=\"Terminus\"]\n"
//    << "\tedge [arrowsize=.3]\n";

// std::string lastJsonStr; // 记录上次 JSON 的字符串表示

void InMemoryExecutionTree::dump() noexcept {

  // bool changed = false;

  // std::cout << "之前的jsontree" << jsonTree << std::endl;

  // Json::StreamWriterBuilder writer;
  // writer["indentation"] = ""; // 保持格式一致，便于比较
  // std::string currentJsonStr = Json::writeString(writer, jsonTree);

  // // 输出调试信息
  // std::cout << "Before:\n";
  // std::cout << "lastJsonStr: " << lastJsonStr << std::endl;
  // std::cout << "currentJsonStr: " << currentJsonStr << std::endl;

  std::vector<const ExecutionTreeNode *> stack;
  stack.push_back(root.getPointer());
  while (!stack.empty()) {
    const ExecutionTreeNode *n = stack.back();
    stack.pop_back();
    // 将指针转换为字符串
    std::stringstream ss;
    ss << n;
    std::string npointerStr = ss.str();
    // ss.str("");  // 清空 stringstream
    Json::Value jsonNode;
    // 创建一个空的 Json::Value 对象
    Json::Value jsonConstraints(Json::arrayValue);
    Json::Value jsonChildren(Json::arrayValue);
    // 遍历klee::ConstraintSet中的约束条件，并将每个约束转换为JsonValue对象
    jsonNode["name"] = npointerStr;
    // jsonNode["pc"] = n->state->pc;

    if (n->state) {
      jsonNode["id"] = n->state->id;
      jsonNode["instsSinceCovNew"] = n->state->instsSinceCovNew;

      KInstruction *pc = n->state->pc;
      jsonNode["pc"] = pc->getSourceLocation();
      KInstruction *prevpc = n->state->prevPC;
      jsonNode["prevPC"] = prevpc->getSourceLocation();

      // // 将 pathOS 和 symPathOS 转换为字符串
      // std::stringstream pathOSStream;
      // pathOSStream << n->state->pathOS;
      // jsonNode["pathOS"] = pathOSStream.str();

      // std::stringstream symPathOSStream;
      // symPathOSStream << n->state->symPathOS;
      // jsonNode["symPathOS"] = symPathOSStream.str();

      jsonNode["steppedInstructions"] = n->state->steppedInstructions;

      // // 将 coveredLines 转换为 JSON 表示
      Json::Value coveredLinesJson(Json::objectValue);
      for (const auto &[fileName, lineSet] : n->state->coveredLines) {
        Json::Value lineArray(Json::arrayValue);
        for (uint32_t line : lineSet) {
          lineArray.append(line);
        }
        coveredLinesJson[*fileName] = lineArray;
      }
      jsonNode["coveredLines"] = coveredLinesJson;

      /*if (kinstruction != nullptr) {
        std::cout << kinstruction;
        std::cout << kinstruction->getSourceLocation();
      } else {
        std::cout << "kinstruction is nullptr." << std::endl;
      }*/
      // jsonNode["sourcecode"] = kinstruction->getSourceLocation();
      if (!n->state->constraints.empty()) {
        for (const auto &constraint : n->state->constraints) {
          // 将 klee::ref<klee::Expr> 转换为 Json::Value
          std::stringstream constraintStr;
          constraintStr << *constraint;
          std::string constraintStrValue = constraintStr.str();
          removeExtraSpacesAndNewlines(constraintStrValue);
          Json::Value jsonConstraint(parseExpression(constraintStrValue));
          jsonConstraints.append(jsonConstraint);
        }
      }
      jsonNode["constraints"] = jsonConstraints;

      // 输出变量

      for (const auto &pair : n->state->addressSpace.objects) {
        const klee::MemoryObject *mo = pair.first;
        const klee::ref<klee::ObjectState> os = pair.second;

        Json::Value jsonObject;
        std::stringstream addrStr;
        addrStr << mo->getBaseExpr(); // 地址
        jsonObject["address"] = addrStr.str();
        jsonObject["size"] = mo->size;
        jsonObject["name"] = mo->name;

        Json::Value byteValues(Json::arrayValue);
        for (unsigned i = 0; i < mo->size; ++i) {
          klee::ref<klee::Expr> byte = os->read8(i);
          std::stringstream byteStr;
          byteStr << *byte;
          std::string valStr = byteStr.str();
          removeExtraSpacesAndNewlines(valStr);
          byteValues.append(valStr);
        }
        jsonObject["bytes"] = byteValues;
        jsonMemoryObjects.append(jsonObject);
      }
      // jsonNode["memoryObjects"] = jsonMemoryObjects;
    }
    // os << "\tn" << n << " [shape=diamond";
    // if (n->state)
    //   os << ",fillcolor=green";
    // os << "];\n";
    if (n->left.getPointer()) {
      // 将指针转换为字符串
      std::stringstream ssl;
      ssl << n->left.getPointer();
      std::string lpointerStr = ssl.str();
      // ssl.str("");  // 清空 stringstream
      Json::Value jsonValuelchild(lpointerStr);
      jsonChildren.append(jsonValuelchild);
      // os << "\tn" << n << " -> n" << n->left.getPointer() << " [label=0b"
      //    << std::bitset<PtrBitCount>(n->left.getInt()).to_string() <<
      //    "];\n";
      stack.push_back(n->left.getPointer());
    }
    if (n->right.getPointer()) {
      // 将指针转换为字符串
      std::stringstream ssr;
      ssr << n->right.getPointer();
      std::string rpointerStr = ssr.str();
      // ssr.str("");  // 清空 stringstream
      Json::Value jsonValuerchild(rpointerStr);
      jsonChildren.append(jsonValuerchild);
      // os << "\tn" << n << " -> n" << n->right.getPointer() << " [label=0b"
      //    << std::bitset<PtrBitCount>(n->right.getInt()).to_string() <<
      //    "];\n";
      stack.push_back(n->right.getPointer());
    }

    bool isExists = false;
    // std::cout << "存在" << isExists << std::endl;

    for (auto &node : jsonTree) {
      if (node["name"] == npointerStr) {
        isExists = true;

        // 如果 node["children"] 为空，则直接赋值
        if (node["children"].empty()) {
          node["children"] = jsonChildren;
          // changed = true;
        } else {
          // 遍历 jsonChildren，并检查是否需要添加到 children 中
          for (const auto &child : jsonChildren) {
            // if (!child.isString()) {
            //   std::cerr << "Error: Child is not a string!" << std::endl;
            //   continue;
            // }

            bool childExists = false;

            // 检查当前 child 是否已存在于 node["children"] 中
            for (const auto &existingChild : node["children"]) {
              if (existingChild.asString() == child.asString()) {
                childExists = true;
                break;
              }
            }

            // 如果当前 child 不存在，则添加到 node["children"]
            if (!childExists) {
              node["children"].append(child);
              // changed = true;
            }
          }
        }
        break;
      }
    }

    // std::cout << "当前节点是" << jsonNode["name"] << "节点是否存在" <<
    // isExists
    //           << "当前的树" << jsonTree << std::endl;

    // 如果节点不存在，则将新节点添加到 jsonTree 中
    if (!isExists) {
      jsonNode["children"] = jsonChildren;
      jsonTree.append(jsonNode);

      // outputToStdout(jsonTree); // 实时输出到 stdout
    }
    // jsonData["jsontree"] = jsonTree;
    // jsonData["type"] = "jsontree";
    // std::cout << jsonData << endl;
    Json::StreamWriterBuilder writer;
    writer["indentation"] = ""; // 不格式化，压缩成一行
    std::string jsonTreeStr = Json::writeString(writer, jsonTree);
    std::cout << jsonTreeStr << std::endl;
    // std::cout << jsonTree << std::endl;
  }

  // std::cout << "到了这里" << endl;

  // currentJsonStr = Json::writeString(writer, jsonTree);

  // // 输出调试信息
  // std::cout << "After:\n";
  // std::cout << "lastJsonStr: " << lastJsonStr << std::endl;
  // std::cout << "currentJsonStr: " << currentJsonStr << std::endl;

  // if (currentJsonStr != lastJsonStr) {
  //   // std::cout << "jsonTree has changed:\n";
  //   outputToStdout(jsonTree);
  //   lastJsonStr = currentJsonStr; // 更新记录
  // }
  // os << "}\n";

  // std::cout << "之后的jsontree" << jsonTree << std::endl;
  // std::cout << "此时的changed" << changed << endl;
  // if (changed) {
  //   outputToStdout(jsonTree);
  // }
}

// 函数：将 JSON 值输出到 stdout
void InMemoryExecutionTree::outputToStdout(const Json::Value &tree) {
  std::cout << "正在输出 JSON..." << std::endl; // 调试信息
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "  "; // 可选：格式化输出
  std::cout << "jsonTree 类型: " << tree.type() << std::endl;

  std::string output = Json::writeString(writer, tree);
  std::cout << "JSON 生成成功，长度: " << output.size() << std::endl;
  std::cout << output << std::endl; // 输出到 stdout
}

void InMemoryExecutionTree::writeToJsonFile(const std::string &filename) {
  std::ofstream outputFile(filename);
  if (outputFile.is_open()) {
    Json::StreamWriterBuilder writerBuilder;
    std::unique_ptr<Json::StreamWriter> writer(writerBuilder.newStreamWriter());
    writer->write(jsonTree, &outputFile);
    outputFile.close();
    std::cout << "JSON file saved successfully." << std::endl;
  } else {
    std::cerr << "Unable to open file for writing." << std::endl;
  }
}

std::uint8_t InMemoryExecutionTree::getNextId() noexcept {
  static_assert(PtrBitCount <= 8);
  std::uint8_t id = 1 << registeredIds++;
  if (registeredIds > PtrBitCount) {
    klee_error("ExecutionTree cannot support more than %d RandomPathSearchers",
               PtrBitCount);
  }
  return id;
}

// PersistentExecutionTree

PersistentExecutionTree::PersistentExecutionTree(
    ExecutionState &initialState, InterpreterHandler &ih) noexcept
    : writer(ih.getOutputFilename("exec_tree.db")) {
  root = ExecutionTreeNodePtr(createNode(nullptr, &initialState));
  initialState.executionTreeNode = root.getPointer();
}

// void PersistentExecutionTree::dump(llvm::raw_ostream &os) noexcept {
//   writer.batchCommit(true);
//   InMemoryExecutionTree::dump(os);
// }
void PersistentExecutionTree::dump() noexcept {
  writer.batchCommit(true);
  InMemoryExecutionTree::dump();
}

ExecutionTreeNode *
PersistentExecutionTree::createNode(ExecutionTreeNode *parent,
                                    ExecutionState *state) {
  return new AnnotatedExecutionTreeNode(parent, state);
}

void PersistentExecutionTree::setTerminationType(ExecutionState &state,
                                                 StateTerminationType type) {
  auto *annotatedNode =
      llvm::cast<AnnotatedExecutionTreeNode>(state.executionTreeNode);
  annotatedNode->kind = type;
}

void PersistentExecutionTree::updateBranchingNode(ExecutionTreeNode &node,
                                                  BranchType reason) {
  auto *annotatedNode = llvm::cast<AnnotatedExecutionTreeNode>(&node);
  const auto &state = *node.state;
  const auto prevPC = state.prevPC;
  annotatedNode->asmLine =
      prevPC && prevPC->info ? prevPC->info->assemblyLine : 0;
  annotatedNode->kind = reason;
  writer.write(*annotatedNode);
}

void PersistentExecutionTree::updateTerminatingNode(ExecutionTreeNode &node) {
  assert(node.state);
  auto *annotatedNode = llvm::cast<AnnotatedExecutionTreeNode>(&node);
  const auto &state = *node.state;
  const auto prevPC = state.prevPC;
  annotatedNode->asmLine =
      prevPC && prevPC->info ? prevPC->info->assemblyLine : 0;
  annotatedNode->stateID = state.getID();
  writer.write(*annotatedNode);
}

// Factory

std::unique_ptr<ExecutionTree>
klee::createExecutionTree(ExecutionState &initialState, bool inMemory,
                          InterpreterHandler &ih) {
  if (WriteExecutionTree)
    return std::make_unique<PersistentExecutionTree>(initialState, ih);

  if (inMemory)
    return std::make_unique<InMemoryExecutionTree>(initialState);

  return std::make_unique<NoopExecutionTree>();
};
