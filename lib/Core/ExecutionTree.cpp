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
#include <regex>
#include <sstream>

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

void NoopExecutionTree::dump(llvm::raw_ostream &os) noexcept {
  os << "digraph G {\nTreeNotAvailable [shape=box]\n}";
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

std::string parseExpression(const std::string &expression) {
  std::regex eqRegex(R"(^\(Eq (\w+) \(ReadLSB w32 0 (\w+)\)\)$)");
  std::regex noteqRegex(
      R"(^\(Eq false \(Eq (\w+) \(ReadLSB w32 0 (\w+)\)\)\)$)");
  std::regex notsltRegex(
      R"(^\(Eq false \(Slt \(ReadLSB w32 0 (\w+)\) (\d+)\)\)$)");
  std::regex sltRegex(R"(^\(Slt \(ReadLSB w32 0 (\w+)\) (\d+)\)$)");

  std::string result = expression;

  if (std::regex_search(result, eqRegex))
    result = std::regex_replace(result, eqRegex, "$2=$1");
  if (std::regex_search(result, noteqRegex))
    result = std::regex_replace(result, noteqRegex, "$2!=$1");
  if (std::regex_search(result, notsltRegex))
    result = std::regex_replace(result, notsltRegex, "$1>=$2");
  if (std::regex_search(result, sltRegex))
    result = std::regex_replace(result, sltRegex, "$1<$2");

  return result;
}

void InMemoryExecutionTree::dump(llvm::raw_ostream &os) noexcept {
  std::unique_ptr<ExprPPrinter> pp(ExprPPrinter::create(os));
  pp->setNewline("\\l");
  os << "digraph G {\n"
     << "\tsize=\"10,7.5\";\n"
     << "\tratio=fill;\n"
     << "\trotate=90;\n"
     << "\tcenter = \"true\";\n"
     << "\tnode [style=\"filled\",width=.1,height=.1,fontname=\"Terminus\"]\n"
     << "\tedge [arrowsize=.3]\n";
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

      KInstruction *kinstruction = n->state->pc;
      jsonNode["pc1"] = kinstruction;
      jsonNode["pc2"] = kinstruction->getSourceLocation();
    }
    /*if (kinstruction != nullptr) {
      std::cout << kinstruction;
      std::cout << kinstruction->getSourceLocation();
    } else {
      std::cout << "kinstruction is nullptr." << std::endl;
    }*/
    // jsonNode["sourcecode"] = kinstruction->getSourceLocation();
    if (n->state) {
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
    }
    jsonNode["constraints"] = jsonConstraints;
    os << "\tn" << n << " [shape=diamond";
    if (n->state)
      os << ",fillcolor=green";
    os << "];\n";
    if (n->left.getPointer()) {
      // 将指针转换为字符串
      std::stringstream ssl;
      ssl << n->left.getPointer();
      std::string lpointerStr = ssl.str();
      // ssl.str("");  // 清空 stringstream
      Json::Value jsonValuelchild(lpointerStr);
      jsonChildren.append(jsonValuelchild);
      os << "\tn" << n << " -> n" << n->left.getPointer() << " [label=0b"
         << std::bitset<PtrBitCount>(n->left.getInt()).to_string() << "];\n";
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
      os << "\tn" << n << " -> n" << n->right.getPointer() << " [label=0b"
         << std::bitset<PtrBitCount>(n->right.getInt()).to_string() << "];\n";
      stack.push_back(n->right.getPointer());
    }
    jsonNode["children"] = jsonChildren;
    bool isExists = false;
    for (auto &node : jsonTree) {
      if (node["name"] == npointerStr) {
        isExists = true;
        if (node["children"] && !jsonChildren.empty()) {
          node["children"] = jsonChildren;
        }
        break;
      }
    }

    if (!isExists) {
      jsonTree.append(jsonNode);
      writeToJsonFile(
          "/home/klee/workdir/examples/get_sign/tree.json"); // 实时写入 JSON
                                                             // 文件
    }
  }
  os << "}\n";
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

void PersistentExecutionTree::dump(llvm::raw_ostream &os) noexcept {
  writer.batchCommit(true);
  InMemoryExecutionTree::dump(os);
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
