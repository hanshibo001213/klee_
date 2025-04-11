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

void NoopExecutionTree::dump() noexcept {
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
  str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());

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
  std::stack<std::string> exprStack;
  std::istringstream stream(expression);
  std::vector<std::string> tokens;
  std::string token;
  bool eqFalsePending = false; 

  std::regex tokenRegex(R"([\(\)]|[^\s\(\)]+)");
  auto tokensBegin =
      std::sregex_iterator(expression.begin(), expression.end(), tokenRegex);
  auto tokensEnd = std::sregex_iterator();

  for (std::sregex_iterator i = tokensBegin; i != tokensEnd; ++i) {
    tokens.push_back(i->str());
  }

  for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
    token = *it;
    if (token == "(" || token == ")") {
      continue;
    } else if (token == "false") {
      eqFalsePending = true; 
    } else if (token == "ReadLSB") {
      std::string w32 = exprStack.top();
      exprStack.pop();
      std::string zero = exprStack.top();
      exprStack.pop();
      std::string variable = exprStack.top();
      exprStack.pop();
      exprStack.push(variable); 
    }

    else if (token == "Eq") {
      std::string operand1 = exprStack.top();
      exprStack.pop();

      if (eqFalsePending) {
        std::regex pattern(
            R"((.*) ([<>=!]+) (.*))"); 
        std::smatch match;

        if (std::regex_match(operand1, match, pattern)) {
          std::string left = match[1].str();
          std::string op = match[2].str();
          std::string right = match[3].str();

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
        eqFalsePending = false; 
      } else {
        std::string operand2 = exprStack.top();
        exprStack.pop();
        exprStack.push(operand1 + " == " + operand2);
      }
    } else if (token == "Ne") 
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
    } else if (token == "Sle") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " <= " + operand2);
    } else if (token == "Sgt") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " > " + operand2);
    } else if (token == "Sge") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " >= " + operand2);
    } else if (token == "Ult") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " < " + operand2);
    } else if (token == "Ule") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " <= " + operand2);
    } else if (token == "Ugt") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " > " + operand2);
    } else if (token == "Uge") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " >= " + operand2);
    }

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
    } else if (token == "UDiv") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " / " + operand2);
    } else if (token == "SDiv") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " / " + operand2);
    } else if (token == "URem") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " % " + operand2);
    } else if (token == "SRem") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push(operand1 + " % " + operand2);
    }

    else if (token == "Not") {
      std::string operand = exprStack.top();
      exprStack.pop();
      exprStack.push("~(" + operand + ")"); 
    } else if (token == "And") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " & " + operand2 + ")");
    } else if (token == "Or") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " | " + operand2 + ")");
    } else if (token == "Xor") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " ^ " + operand2 + ")");
    } else if (token == "Shl") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " << " + operand2 + ")");
    } else if (token == "LShr") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " >> " + operand2 + ")");
    } else if (token == "AShr") 
    {
      std::string operand1 = exprStack.top();
      exprStack.pop();
      std::string operand2 = exprStack.top();
      exprStack.pop();
      exprStack.push("(" + operand1 + " >> " + operand2 + ")");
    }

    else {
      exprStack.push(token);
    }
  }

  return exprStack.empty() ? "" : exprStack.top();
}

void InMemoryExecutionTree::dump() noexcept {

  std::vector<const ExecutionTreeNode *> stack;
  stack.push_back(root.getPointer());
  while (!stack.empty()) {
    const ExecutionTreeNode *n = stack.back();
    stack.pop_back();
    std::stringstream ss;
    ss << n;
    std::string npointerStr = ss.str();
    Json::Value jsonNode;
    Json::Value jsonConstraints(Json::arrayValue);
    Json::Value jsonChildren(Json::arrayValue);
    jsonNode["name"] = npointerStr;

    if (n->state) {
      jsonNode["id"] = n->state->id;
      jsonNode["instsSinceCovNew"] = n->state->instsSinceCovNew;

      KInstruction *pc = n->state->pc;
      jsonNode["pc"] = pc->getSourceLocation();
      KInstruction *prevpc = n->state->prevPC;
      jsonNode["prevPC"] = prevpc->getSourceLocation();

      jsonNode["steppedInstructions"] = n->state->steppedInstructions;

      Json::Value coveredLinesJson(Json::objectValue);
      for (const auto &[fileName, lineSet] : n->state->coveredLines) {
        Json::Value lineArray(Json::arrayValue);
        for (uint32_t line : lineSet) {
          lineArray.append(line);
        }
        coveredLinesJson[*fileName] = lineArray;
      }
      jsonNode["coveredLines"] = coveredLinesJson;
        
      if (!n->state->constraints.empty()) {
        for (const auto &constraint : n->state->constraints) {
          std::stringstream constraintStr;
          constraintStr << *constraint;
          std::string constraintStrValue = constraintStr.str();
          removeExtraSpacesAndNewlines(constraintStrValue);
          Json::Value jsonConstraint(parseExpression(constraintStrValue));
          jsonConstraints.append(jsonConstraint);
        }
      }
      jsonNode["constraints"] = jsonConstraints;

      for (const auto &pair : n->state->addressSpace.objects) {
        const klee::MemoryObject *mo = pair.first;
        const klee::ref<klee::ObjectState> os = pair.second;

        Json::Value jsonObject;
        std::stringstream addrStr;
        addrStr << mo->getBaseExpr();
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
    if (n->left.getPointer()) {
      std::stringstream ssl;
      ssl << n->left.getPointer();
      std::string lpointerStr = ssl.str();
      Json::Value jsonValuelchild(lpointerStr);
      jsonChildren.append(jsonValuelchild);
      stack.push_back(n->left.getPointer());
    }
    if (n->right.getPointer()) {
      std::stringstream ssr;
      ssr << n->right.getPointer();
      std::string rpointerStr = ssr.str();
      Json::Value jsonValuerchild(rpointerStr);
      jsonChildren.append(jsonValuerchild);
      stack.push_back(n->right.getPointer());
    }

    bool isExists = false;

    for (auto &node : jsonTree) {
      if (node["name"] == npointerStr) {
        isExists = true;

        if (node["children"].empty()) {
          node["children"] = jsonChildren;
        } else {
          for (const auto &child : jsonChildren) {
              
            bool childExists = false;

            for (const auto &existingChild : node["children"]) {
              if (existingChild.asString() == child.asString()) {
                childExists = true;
                break;
              }
            }

            if (!childExists) {
              node["children"].append(child);
            }
          }
        }
        break;
      }
    }

    if (!isExists) {
      jsonNode["children"] = jsonChildren;
      jsonTree.append(jsonNode);
    }

    Json::StreamWriterBuilder writer;
    writer["indentation"] = ""; 
    std::string jsonTreeStr = Json::writeString(writer, jsonTree);
    std::cout << jsonTreeStr << std::endl;
  }
}

void InMemoryExecutionTree::outputToStdout(const Json::Value &tree) {
  std::cout << "正在输出 JSON..." << std::endl; 
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "  "; 
  std::cout << "jsonTree 类型: " << tree.type() << std::endl;

  std::string output = Json::writeString(writer, tree);
  std::cout << "JSON 生成成功，长度: " << output.size() << std::endl;
  std::cout << output << std::endl; 
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
