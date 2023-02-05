/*
  csound_orc_expressions.c:

  Copyright (C) 2006
  Steven Yi

  This file is part of Csound.

  The Csound Library is free software; you can redistribute it
  and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  Csound is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with Csound; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
  02110-1301 USA
*/

#include "csoundCore.h"
#include "csound_orc.h"
#include "csound_orc_expressions.h"
#include "csound_type_system.h"
#include "csound_orc_semantics.h"
#include <inttypes.h>

extern char argtyp2(char *);
extern void print_tree(CSOUND *, char *, TREE *);
extern void handle_optional_args(CSOUND *, TREE *);
extern ORCTOKEN *make_token(CSOUND *, char *);
extern ORCTOKEN *make_label(CSOUND *, char *);
extern OENTRIES* find_opcode2(CSOUND *, char*);
extern char* resolve_opcode_get_outarg(CSOUND* , OENTRIES* , char*);
extern TREE* appendToTree(CSOUND * csound, TREE *first, TREE *newlast);
extern  char* get_arg_string_from_tree(CSOUND*, TREE*, TYPE_TABLE*);
extern void add_arg(CSOUND* csound, char* varName, char* annotation, TYPE_TABLE* typeTable);
extern void add_array_arg(CSOUND* csound, char* varName, char* annotation, int dimensions,
                          TYPE_TABLE* typeTable);

extern char* get_array_sub_type(CSOUND* csound, char*);

extern char* convert_external_to_internal(CSOUND* csound, char* arg);


static TREE *create_boolean_expression(CSOUND*, TREE*, int, int, TYPE_TABLE*);
static TREE *create_expression(CSOUND *, TREE *, TREE *, int, int, TYPE_TABLE*);
char *check_annotated_type(CSOUND* csound, OENTRIES* entries,
                           char* outArgTypes);
static TREE *create_synthetic_label(CSOUND *csound, int32 count);
static TREE *create_synthetic_ident(CSOUND*, int32);
extern void do_baktrace(CSOUND *csound, uint64_t files);
extern CS_TYPE* csoundGetTypeWithVarTypeName(TYPE_POOL*, char*);




static int genlabs = 300;

TREE* tree_tail(TREE* node) {
  TREE* t = node;
  if (t == NULL) {
    return NULL;
  }
  while (t->next != NULL) {
    t = t->next;
  }
  return t;
}

char *create_out_arg(CSOUND *csound, char* outype, int argCount,
                     TYPE_TABLE* typeTable)
{
  char* s = (char *)csound->Malloc(csound, 256);
  if (strlen(outype) == 1) {
    switch(*outype) {
    case 'a': snprintf(s, 16, "#a%d", argCount); break;
    case 'K':
    case 'k': snprintf(s, 16, "#k%d", argCount); break;
    case 'B': snprintf(s, 16, "#B%d", argCount); break;
    case 'b': snprintf(s, 16, "#b%d", argCount); break;
    case 'f': snprintf(s, 16, "#f%d", argCount); break;
    case 't': snprintf(s, 16, "#k%d", argCount); break;
    case 'S': snprintf(s, 16, "#S%d", argCount); break;
    case '[': snprintf(s, 16, "#%c%d[]", outype[1], argCount);
      break;
    default:  snprintf(s, 16, "#i%d", argCount); break;
    }
    add_arg(csound, s, NULL, typeTable);
  } else {
    // FIXME - struct arrays
    if (*outype == '[') {
      snprintf(s, 16, "#%c%d[]", outype[1], argCount);
      add_array_arg(csound, s, NULL, 1, typeTable);
    } else {
      //            char* argType = cs_strndup(csound, outype + 1, strlen(outype) - 2);
      snprintf(s, 256, "#%s%d", outype, argCount);
      add_arg(csound, s, outype, typeTable);
    }
    //        } else if(*outype == ':') {
    //            char* argType = cs_strndup(csound, outype + 1, strlen(outype) - 2);
    //            snprintf(s, 256, "#%s%d", argType, argCount);
    //            add_arg(csound, s, argType, typeTable);
    //        } else {
    //            csound->Warning(csound, "ERROR: unknown outype found for out arg synthesis: %s\n", outype);
    //            return NULL;
    //        }
  }

  return s;
}

/**
 * Handles expression opcode type, appending to passed in opname
 * returns outarg type
 */

char * get_boolean_arg(CSOUND *csound, TYPE_TABLE* typeTable, int type)
{
  char* s = (char *)csound->Malloc(csound, 8);
  snprintf(s, 8, "#%c%d", type?'B':'b', typeTable->localPool->synthArgCount++);

  return s;
}

static TREE *create_empty_token(CSOUND *csound)
{
  TREE *ans;
  ans = (TREE*)csound->Malloc(csound, sizeof(TREE));
  if (UNLIKELY(ans==NULL)) {
    /* fprintf(stderr, "Out of memory\n"); */
    exit(1);
  }
  ans->type = -1;
  ans->left = NULL;
  ans->right = NULL;
  ans->next = NULL;
  ans->len = 0;
  ans->rate = -1;
  ans->line = 0;
  ans->locn  = 0;
  ans->value = NULL;
  ans->markup = NULL;
  return ans;
}

static TREE *create_minus_token(CSOUND *csound)
{
  TREE *ans;
  ans = (TREE*)csound->Malloc(csound, sizeof(TREE));
  if (UNLIKELY(ans==NULL)) {
    /* fprintf(stderr, "Out of memory\n"); */
    exit(1);
  }
  ans->type = INTEGER_TOKEN;
  ans->left = NULL;
  ans->right = NULL;
  ans->next = NULL;
  ans->len = 0;
  ans->rate = -1;
  ans->markup = NULL;
  ans->value = make_int(csound, "-1");
  return ans;
}

// also used in csound_orc_semantics.c
TREE * create_opcode_token(CSOUND *csound, char* op)
{
  TREE *ans = create_empty_token(csound);

  ans->type = T_OPCALL;
  ans->value = make_token(csound, op);
  ans->value->type = T_OPCALL;

  return ans;
}

static TREE * create_ans_token(CSOUND *csound, char* var)
{
  TREE *ans = create_empty_token(csound);

  ans->type = T_IDENT;
  ans->value = make_token(csound, var);
  ans->value->type = ans->type;

  return ans;

}

static TREE * create_goto_token(CSOUND *csound, char * booleanVar,
                                TREE * gotoNode, int type)
{
  /*     TREE *ans = create_empty_token(csound); */
  char* op = (char *)csound->Malloc(csound, 8); /* Unchecked */
  TREE *opTree, *bVar;

  switch(gotoNode->type) {
  case KGOTO_TOKEN:
    strNcpy(op, "ckgoto", 8);
    break;
  case IGOTO_TOKEN:
    strNcpy(op, "cigoto", 8);
    break;
  case ITHEN_TOKEN:
    // *** yi ***
  icase:
    strNcpy(op, "cingoto", 8);
    break;
  case THEN_TOKEN:
    // *** yi ***
    if (csound->inZero) goto icase;
    /* if (csound->inZero) { */
    /*   printf("**** Odd case in instr0 %d\n", csound->inZero); */
    /*   print_tree(csound, "goto token\n", gotoNode); */
    /* } */
    /* fall through */
  case KTHEN_TOKEN:
    strNcpy(op, "cngoto", 8);
    break;
  default:
    switch (type) {
    case 1: strNcpy(op, "ckgoto", 8); break;
    case 0x8001: strNcpy(op, "cnkgoto", 8); break;
    case 0: strNcpy(op, "cggoto", 8); break;
    case 0x8000:
      // *** yi ***
      strNcpy(op,csound->inZero?"cingoto":"cngoto", 8);
      //strNcpy(op,"cngoto", 8);
      break;
    default: printf("Whooops %d\n", type);
    }
  }

  opTree = create_opcode_token(csound, op);
  bVar = create_empty_token(csound);
  bVar->type = T_IDENT; //(type ? T_IDENT_B : T_IDENT_b);
  bVar->value = make_token(csound, booleanVar);
  bVar->value->type = bVar->type;

  opTree->left = NULL;
  opTree->right = bVar;
  opTree->right->next = gotoNode->right;
  csound->Free(csound, op);
  return opTree;
}

/* THIS PROBABLY NEEDS TO CHANGE TO RETURN DIFFERENT GOTO
   TYPES LIKE IGOTO, ETC */
static TREE *create_simple_goto_token(CSOUND *csound, TREE *label, int type)
{
  char* op = (char *)csound->Calloc(csound, 6);
  TREE * opTree;
  char *gt[3] = {"kgoto", "igoto", "goto"};
  if (csound->inZero && type==2) type = 1;
  strNcpy(op, gt[type],6);       /* kgoto, igoto, goto ?? */
  opTree = create_opcode_token(csound, op);
  opTree->left = NULL;
  opTree->right = label;
  csound->Free(csound, op);
  return opTree;
}

/* Returns true if passed in TREE node is a numerical expression */
int is_expression_node(TREE *node)
{
  if (node == NULL) {
    return 0;
  }

  switch(node->type) {
  case '+':
  case '-':
  case '*':
  case '/':
  case '%':
  case '^':
  case T_FUNCTION:
  case S_UMINUS:
  case S_UPLUS:
  case '|':
  case '&':
  case S_BITSHIFT_RIGHT:
  case S_BITSHIFT_LEFT:
  case '#':
  case '~':
  case '?':
  case T_ARRAY:
  case STRUCT_EXPR:
    return 1;
  }
  return 0;
}

/* Returns if passed in TREE node is a boolean expression */
int is_boolean_expression_node(TREE *node)
{
  if (node == NULL) {
    return 0;
  }

  switch(node->type) {
  case S_EQ:
  case S_NEQ:
  case S_GE:
  case S_LE:
  case S_GT:
  case S_LT:
  case S_AND:
  case S_OR:
  case S_UNOT:
    return 1;
  }
  return 0;
}

extern int check_satisfies_expected_input(
    CS_TYPE*, char* , int
);

static TREE *create_ternay_expression(
  CSOUND *csound,
  TREE *root,
  int line,
  int locn,
  TYPE_TABLE* typeTable)
{
  TREE *last = NULL;
  int32 ln1 = genlabs++, ln2 = genlabs++;
  TREE *L1 = create_synthetic_label(csound, ln1);
  TREE *L2 = create_synthetic_label(csound, ln2);
  TREE *b = create_boolean_expression(csound, root->left, line, locn,
                                      typeTable);
  TREE *c = root->right->left, *d = root->right->right;
  char *left, *right;
  int type;
  TREE *xx;
  char *eq;

  typeTable->labelList =
    cs_cons(csound,
            cs_strdup(csound, L1->value->lexeme), typeTable->labelList);
  typeTable->labelList =
    cs_cons(csound,
            cs_strdup(csound, L2->value->lexeme), typeTable->labelList);
  //print_tree(csound, "***B\n", b);
  //print_tree(csound, "***C\n", c); print_tree(csound,"***D\n", d);
  left = get_arg_type2(csound, c, typeTable);
  right  = get_arg_type2(csound, d, typeTable);
  //printf("***types %s %s\n", left, right);
  if (left[0]=='c') left[0] = 'i';
  if (right[0]=='c') right[0] = 'i';
  //printf("***type = %c %c\n",left[0], right[0]);
  last = b;
  while (last->next != NULL) {
    last = last->next;
  }

  //p{rintf("type = %s , %s\n", left, right);
  if (left[0]=='S' || right[0]=='S') {
    type = (last->left->value->lexeme[1]=='B') ? 2 : 1;
    eq = (last->left->value->lexeme[1]=='B') ?"#=.S" : "=.S";
  }
  else if (left[0] == 'a' && right[0] == 'a') {
    type = 0;
    eq = "=";
  }
  else if (left[0]=='a' || right[0]=='a') {
    csound->Warning(csound, Str("Unbalanced rates in conditional expression"));
    return NULL;
  }
  else {
    type =
      (left[0]=='k' || right[0]=='k' || last->left->value->lexeme[1]=='B') ?2 : 1;
    if (type==2) left[0] = right[0] = 'k';
    eq = "=";
  }
  //printf("***boolvalr = %s, type=%d\n", last->left->value->lexeme, type);
  //print_tree(csound, "***\nL1\n", L1);

  // verify the rates
  int error = 0;
  if (strlen(left) != strlen(right)) {
    // user defined types or arrays will never match
    error = 1;
  } else if (strlen(left) == 1) {
    // assuming the code is correct and if one matches they
    // should also match the othwer way around
    CS_TYPE* leftType = csoundFindStandardTypeWithChar(left[0]);
    if (leftType != NULL && !check_satisfies_expected_input(leftType, right, 0)) {
      error = 1;
    }
  }

  if (error) {
    printf("\n");
    synterr(
      csound,
      Str("Unable to find ternary operator for types "
                " '@ ? %s : %s'  \n"),
            left,
            right
        );

    return NULL;
  }

  last->next = create_opcode_token(csound, type==1?"cigoto":"ckgoto");
  //print_tree(csound, "first jump\n", last->next);
  xx = create_empty_token(csound);
  xx->type = T_IDENT;
  xx->value = make_token(csound, last->left->value->lexeme);
  xx->value->type = T_IDENT;
  last = last->next;
  last->left = NULL;
  last->right = xx;
  last->right->next = L1;
  last->line = line; root->locn = locn;
  //print_tree(csound, "***IF node\n", b);
  // Need to get type of expression for newvariable
  right = create_out_arg(csound,left,
                         typeTable->localPool->synthArgCount++, typeTable);
  //printf("right = %s\n", right);
  {
    TREE *C = create_opcode_token(csound, cs_strdup(csound, eq));
    C->left = create_ans_token(csound, right); C->right = c;
    c = C;
  }
  //print_tree(csound, "\n\nc\n", c);
  {
    TREE *D = create_opcode_token(csound, cs_strdup(csound, eq));
    D->left = create_ans_token(csound, right); D->right = d;
    d = D;
  }
  //print_tree(csound, "\n\nc\n", c);
  //print_tree(csound, "\n\nd\n", d);
  last = b;
  while (last->next != NULL) {
    last = last->next;
  }
  last->next = d;
  while (last->next != NULL) last = last->next;
  //Last is now last assignment
  //print_tree(csound, "\n\nlast assignment\n", last);
  //printf("=======type = %d\n", type);
  last->next = create_simple_goto_token(csound, L2, type==2?0:type);
  //print_tree(csound, "second goto\n", last->next);
  //print_tree(csound, "\n\nafter goto\n", b);
  while (last->next != NULL) last = last->next;
  last->next = create_synthetic_label(csound,ln1);
  while (last->next != NULL) last = last->next;
  //print_tree(csound, "\n\nafter label\n", b);

  last->next = c;
  while (last->next != NULL) last = last->next;
  //print_tree(csound, "n\nAfter c\n", b);
  while (last->next != NULL) last = last->next;
  last->next = create_synthetic_label(csound,ln2);
  //print_tree(csound, "\n\nafter secondlabel\n", b);
  while (last->next != NULL) last = last->next;
  last->next = create_opcode_token(csound, cs_strdup(csound, eq));
  //print_tree(csound, "\n\nafter secondlabel\n", b);
  last->next->left = create_ans_token(csound, right);
  last->next->right = create_ans_token(csound, right);

  //printf("\n\n*** create_cond_expression ends\n");

  //print_tree(csound, "ANSWER\n", b);
  return b;
}

/**
 * Create a chain of Opcode (OPTXT) text from the AST node given. Called from
 * create_opcode when an expression node has been found as an argument
 */
static TREE *create_expression(
  CSOUND *csound, TREE *root, TREE *parent,
  int line, int locn,
  TYPE_TABLE* typeTable
) {
  char op[80], *outarg = NULL;
  TREE *anchor = NULL, *last;
  TREE * opTree, *current, *newArgList, *previous;

  /* HANDLE SUB EXPRESSIONS */

  if (root->type=='?') {
    return create_ternay_expression(
      csound, root, line,
      locn, typeTable
    );
  }

  memset(op, 0, 80);
  current = root->left;
  previous = root;
  newArgList = NULL;
  while (current != NULL) {
    if (current->type == STRUCT_EXPR) {
      TREE* opcodeCallNode = create_opcode_token(csound, "##member_get");
      TREE* syntheticIdent = create_synthetic_ident(csound, genlabs++);
      if (current->left->type == STRUCT_EXPR) {
        anchor = appendToTree(csound, anchor,
                            create_expression(
                              csound, current->left,
                              previous, line, locn,
                              typeTable
                            ));
        last = tree_tail(anchor);
        opcodeCallNode->right = copy_node(csound, last->left);
        opcodeCallNode->right->next = current->right;
        if (opcodeCallNode->right->next->value->optype == NULL) {
          opcodeCallNode->right->next->value->optype = csound->Strdup(
            csound, opcodeCallNode->right->value->lexeme
          );
        }
      } else if (current->left->type == T_ARRAY) {
        anchor = appendToTree(csound, anchor,
                            create_expression(
                              csound, current->left,
                              previous, line, locn,
                              typeTable
                            ));
        last = tree_tail(anchor);
        opcodeCallNode->right = copy_node(csound, last->left);
        opcodeCallNode->right->next = current->right;
      } else {
        opcodeCallNode->right = current->left;
        opcodeCallNode->right->next = current->right;
      }

      current->right = NULL;
      opcodeCallNode->left = syntheticIdent;
      if (newArgList == NULL) {
        newArgList = copy_node(csound, syntheticIdent);
      } else {
        appendToTree(csound, newArgList, copy_node(csound, syntheticIdent));
      }
      anchor = appendToTree(csound, anchor, opcodeCallNode);
      previous = current;
      current = current->next;

    }
    else
    if (is_expression_node(current)) {
      TREE* newArg;

      anchor = appendToTree(csound, anchor,
                            create_expression(
                              csound, current,
                              previous, line, locn,
                              typeTable
                            ));
      last = tree_tail(anchor);
      newArg = create_ans_token(csound, last->left->value->lexeme);
      newArgList = appendToTree(csound, newArgList, newArg);
      previous = current;
      current = current->next;
    } else {
      TREE* temp;
      newArgList = appendToTree(csound, newArgList, current);
      temp = current->next;
      current->next = NULL;
      previous = current;
      current = temp;
    }

  }
  root->left = newArgList;

  current = root->right;
  previous = root;
  newArgList = NULL;
  while (current != NULL) {
    if (is_expression_node(current)) {
      TREE* newArg;

      anchor = appendToTree(csound, anchor,
                            create_expression(csound, current, previous, line,
                                              locn, typeTable));
      last = tree_tail(anchor);

      newArg = create_ans_token(csound, last->left->value->lexeme);
      newArgList = appendToTree(csound, newArgList, newArg);
      previous = current;
      current = current->next;
    }
    else {
      TREE* temp;
      newArgList = appendToTree(csound, newArgList, current);
      temp = current->next;
      current->next = NULL;
      previous = current;
      current = temp;
    }
  }
  root->right = newArgList;

  switch(root->type) {
  case STRUCT_EXPR: {
    strNcpy(op, "##member_get", 80);
    break;
  }
  case '+':
    strNcpy(op, "##add", 80);
    break;
  case '-':
    strNcpy(op, "##sub", 80);
    break;
  case '*':
    strNcpy(op, "##mul", 80);
    break;
  case '%':
    strNcpy(op, "##mod", 80);
    break;
  case '/':
    strNcpy(op, "##div", 80);
    break;
  case '^':
    strNcpy(op, "##pow", 80);
    break;
  case T_FUNCTION:
    strNcpy(op, root->value->lexeme, strlen(root->value->lexeme) + 1);
    break;
  case S_UMINUS:
    if (UNLIKELY(PARSER_DEBUG))
      csound->Message(csound, "HANDLING UNARY MINUS!");
    root->left = create_minus_token(csound);
    //      arg1 = 'i';
    strNcpy(op, "##mul", 80);
    break;
   case S_UPLUS:
    if (UNLIKELY(PARSER_DEBUG))
      csound->Message(csound, "HANDLING UNARY PLUS!");
    root->left = create_minus_token(csound);
    //      arg1 = 'i';
    strNcpy(op, "##mul", 80);
    break;

  case '|':
    strNcpy(op, "##or", 80);
    break;
  case '&':
    strNcpy(op, "##and", 80);
    break;
  case S_BITSHIFT_RIGHT:
    strNcpy(op, "##shr", 80);
    break;
  case S_BITSHIFT_LEFT:
    strNcpy(op, "##shl", 80);
    break;
  case '#':
    strNcpy(op, "##xor", 80);
    break;
  case '~':
    strNcpy(op, "##not", 80);
    break;
  case T_ARRAY:
    {
      int isStructArray = parent != NULL &&
        (parent->type == STRUCT_EXPR ||
          (parent->left != NULL && parent->left->type == STRUCT_EXPR));
      strNcpy(
        op,
        isStructArray ? "##array_get_struct" : "##array_get",
        80);
      if (outarg != NULL) break;
      char* outype = strdup(".");
      outarg = create_out_arg(
        csound, outype, typeTable->localPool->synthArgCount++, typeTable
      );
      break;
    }

    break;
    /* it should not get here, but if it does,
       return NULL */
  default:
    return NULL;
  }
  opTree = create_opcode_token(csound, op);
  if (root->value) opTree->value->optype = root->value->optype;
  if (root->left != NULL) {
    opTree->right = root->left;
    opTree->right->next = root->right;
    opTree->left = create_synthetic_ident(csound, genlabs++);
    opTree->line = line;
    opTree->locn = locn;
    //print_tree(csound, "making expression", opTree);
  }
  else {
    opTree->right = root->right;
    opTree->left = create_synthetic_ident(csound, genlabs++);
    opTree->line = line;
    opTree->locn = locn;
    if (
      opTree->value != NULL &&
      opTree->value->optype != NULL
    ) {
      opTree->left->value->optype = csound->Strdup(
        csound, opTree->value->optype
      );
    }

  }
  if (anchor == NULL) {
    anchor = opTree;
  }
  else {
    last = anchor;
    while (last->next != NULL) {
      last = last->next;
    }
    last->next = opTree;
  }
  csound->Free(csound, outarg);
  return anchor;
}

/**
 * Create a chain of Opcode (OPTXT) text from the AST node given. Called from
 * create_opcode when an expression node has been found as an argument
 */
static TREE *create_boolean_expression(CSOUND *csound, TREE *root,
                                       int line, int locn, TYPE_TABLE* typeTable)
{
  char *op, *outarg;
  TREE *anchor = NULL, *last;
  TREE * opTree;

  if (UNLIKELY(PARSER_DEBUG))
    csound->Message(csound, "Creating boolean expression\n");
  /* HANDLE SUB EXPRESSIONS */
  if (is_boolean_expression_node(root->left)) {
    anchor = create_boolean_expression(csound, root->left,
                                       line, locn, typeTable);
    last = anchor;
    while (last->next != NULL) {
      last = last->next;
    }
    /* TODO - Free memory of old left node
       freetree */
    root->left = create_ans_token(csound, last->left->value->lexeme);
  } else if (is_expression_node(root->left)) {
    anchor = create_expression(csound, root->left, root, line, locn, typeTable);

    /* TODO - Free memory of old left node
       freetree */
    last = anchor;
    while (last->next != NULL) {
      last = last->next;
    }
    root->left = create_ans_token(csound, last->left->value->lexeme);
  }


  if (is_boolean_expression_node(root->right)) {
    TREE * newRight = create_boolean_expression(csound,
                                                root->right, line, locn,
                                                typeTable);
    if (anchor == NULL) {
      anchor = newRight;
    }
    else {
      last = anchor;
      while (last->next != NULL) {
        last = last->next;
      }
      last->next = newRight;
    }
    last = newRight;

    while (last->next != NULL) {
      last = last->next;
    }
    /* TODO - Free memory of old right node
       freetree */
    root->right = create_ans_token(csound, last->left->value->lexeme);
  }
  else if (is_expression_node(root->right)) {
    TREE * newRight = create_expression(csound, root->right, root, line,
                                        locn, typeTable);

    if (anchor == NULL) {
      anchor = newRight;
    }
    else {
      last = anchor;
      while (last->next != NULL) {
        last = last->next;
      }
      last->next = newRight;
    }
    last = newRight;

    while (last->next != NULL) {
      last = last->next;
    }

    /* TODO - Free memory of old right node
       freetree */
    root->right = create_ans_token(csound, last->left->value->lexeme);
    root->line = line;
    root->locn = locn;
  }

  op = csound->Calloc(csound, 80);
  switch(root->type) {
  case S_UNOT:
    strNcpy(op, "!", 80);
    break;
  case S_EQ:
    strNcpy(op, "==", 80);
    break;
  case S_NEQ:
    strNcpy(op, "!=", 80);
    break;
  case S_GE:
    strNcpy(op, ">=", 80);
    break;
  case S_LE:
    strNcpy(op, "<=", 80);
    break;
  case S_GT:
    strNcpy(op, ">", 80);
    break;
  case S_LT:
    strNcpy(op, "<", 80);
    break;
  case S_AND:
    strNcpy(op, "&&", 80);
    break;
  case S_OR:
    strNcpy(op, "||", 80);
    break;
  }

  if (UNLIKELY(PARSER_DEBUG)) {
    if (root->type == S_UNOT)
      csound->Message(csound, "Operator Found: %s (%c)\n", op,
                      argtyp2( root->left->value->lexeme));
    else
      csound->Message(csound, "Operator Found: %s (%c %c)\n", op,
                      argtyp2( root->left->value->lexeme),
                      argtyp2( root->right->value->lexeme));
  }
  if (root->type == S_UNOT) {
    outarg = get_boolean_arg(csound,
                             typeTable,
                             argtyp2( root->left->value->lexeme) =='k' ||
                             argtyp2( root->left->value->lexeme) =='B');
  }
  else
    outarg = get_boolean_arg(csound,
                             typeTable,
                             argtyp2( root->left->value->lexeme) =='k' ||
                             argtyp2( root->right->value->lexeme)=='k' ||
                             argtyp2( root->left->value->lexeme) =='B' ||
                             argtyp2( root->right->value->lexeme)=='B');

  add_arg(csound, outarg, NULL, typeTable);
  opTree = create_opcode_token(csound, op);
  opTree->right = root->left;
  opTree->right->next = root->right;
  opTree->left = create_ans_token(csound, outarg);
  if (anchor == NULL) {
    anchor = opTree;
  }
  else {
    last = anchor;
    while (last->next != NULL) {
      last = last->next;
    }
    last->next = opTree;
  }
  csound->Free(csound, outarg);
  csound->Free(csound, op);
  return anchor;
}

static char* create_synthetic_var_name(CSOUND* csound, int32 count, int prefix)
{
  char *name = (char *)csound->Calloc(csound, 36);
  snprintf(name, 36, "%c__synthetic_%"PRIi32, prefix, count);
  return name;
}

static char* create_synthetic_array_var_name(CSOUND* csound, int32 count, int prefix)
{
  char *name = (char *)csound->Calloc(csound, 36);
  snprintf(name, 36, "%c__synthetic_%"PRIi32"[]", prefix, count);
  return name;
}

static TREE *create_synthetic_ident(CSOUND *csound, int32 count)
{
  char *label = (char *)csound->Calloc(csound, 32);
  ORCTOKEN *token;
  snprintf(label, 32, "#_%"PRIi32, count);
  if (UNLIKELY(PARSER_DEBUG))
    csound->Message(csound, "Creating Synthetic T_IDENT: %s\n", label);
  token = make_token(csound, label);
  token->type = T_IDENT;
  csound->Free(csound, label);
  return make_leaf(csound, -1, 0, T_IDENT, token);
}

static TREE *create_synthetic_label_ident(CSOUND *csound, int32 count)
{
  char *label = (char *)csound->Calloc(csound, 32);
  ORCTOKEN *token;
  snprintf(label, 32, "__synthetic_%"PRIi32, count);
  if (UNLIKELY(PARSER_DEBUG))
    csound->Message(csound, "Creating Synthetic label T_IDENT: %s\n", label);
  token = make_token(csound, label);
  token->optype = csound->Strdup(csound, "l");
  token->type = T_IDENT;
  csound->Free(csound, label);
  return make_leaf(csound, -1, 0, T_IDENT, token);
}

static TREE *create_synthetic_label(CSOUND *csound, int32 count)
{
  char *label = (char *)csound->Calloc(csound, 32);
  ORCTOKEN *token;
  snprintf(label, 32, "__synthetic_%"PRIi32":", count);
  if (UNLIKELY(PARSER_DEBUG))
    csound->Message(csound, "Creating Synthetic label: %s\n", label);
  token = make_label(csound, label);
  if (UNLIKELY(PARSER_DEBUG))
    printf("**** label lexeme >>%s<<\n", token->lexeme);
  csound->Free(csound, label);
  return make_leaf(csound, -1, 0, LABEL_TOKEN, token);
}

void handle_negative_number(CSOUND* csound, TREE* root)
{
  if (root->type == S_UMINUS &&
      (root->right->type == INTEGER_TOKEN || root->right->type == NUMBER_TOKEN)) {
    int len = strlen(root->right->value->lexeme);
    char* negativeNumber = csound->Malloc(csound, len + 3);
    negativeNumber[0] = '-';
    strcpy(negativeNumber + 1, root->right->value->lexeme);
    negativeNumber[len + 2] = '\0';
    root->type = root->right->type;
    root->value = root->right->type == INTEGER_TOKEN ?
      make_int(csound, negativeNumber) : make_num(csound, negativeNumber);
    root->value->lexeme = negativeNumber;
  }
}

/* returns the head of a list of TREE* nodes, expanding all RHS
   expressions into statements prior to the original statement line,
   and LHS expressions (array sets) after the original statement
   line */
TREE* expand_statement(CSOUND* csound, TREE* current, TYPE_TABLE* typeTable)
{
  /* This is WRONG in optional argsq */
  TREE* anchor = NULL;
  TREE* originalNext = current->next;

  TREE* previousArg = NULL;
  TREE* currentArg = current->right;

  current->next = NULL;

  if (UNLIKELY(PARSER_DEBUG))
    csound->Message(csound, "Found Statement.\n");
  while (currentArg != NULL) {
    TREE* last;
    TREE *nextArg;
    TREE *newArgTree;
    TREE *expressionNodes;
    int is_bool = 0;
    handle_negative_number(csound, currentArg);

    if (currentArg->type == STRUCT_EXPR) {
        TREE* syntheticIdent = create_synthetic_ident(csound, genlabs++);
        TREE* opcodeCallNode = create_opcode_token(csound, "##member_get");
        TREE* subExpr = NULL;

        if (currentArg->left->type == STRUCT_EXPR) {
          subExpr = create_expression(
            csound,
            currentArg->left,
            currentArg,
            currentArg->line,
            currentArg->locn,
            typeTable
          );
          currentArg->left = NULL;
          opcodeCallNode->right = copy_node(
            csound, subExpr->left
          );
          anchor = appendToTree(csound, anchor, subExpr);
        } else if (currentArg->left->type == T_ARRAY) {
          subExpr = create_expression(
            csound,
            currentArg->left,
            currentArg,
            currentArg->line,
            currentArg->locn,
            typeTable
          );
          currentArg->left = NULL;
          opcodeCallNode->right = copy_node(
            csound, subExpr->left
          );
          anchor = appendToTree(csound, anchor, subExpr);
        } else {
          opcodeCallNode->right = currentArg->left;
        }
        if (currentArg->right->value->optype == NULL) {
          currentArg->right->value->optype = csound->Strdup(
            csound, opcodeCallNode->right->value->lexeme
          );
        }
        opcodeCallNode->right->next = currentArg->right;
        opcodeCallNode->left = copy_node(csound, syntheticIdent);
        if (previousArg == NULL) {
          current->right = syntheticIdent;
          syntheticIdent->next = currentArg->next;
        } else {
          previousArg->next = syntheticIdent;
          syntheticIdent->next = currentArg->next;
        }
        currentArg = syntheticIdent;
        anchor = appendToTree(csound, anchor, opcodeCallNode);
    } else if (is_expression_node(currentArg) ||
        (is_bool = is_boolean_expression_node(currentArg))) {
      char * newArg;
      if (UNLIKELY(PARSER_DEBUG))
        csound->Message(csound, "Found Expression.\n");
      if (is_bool == 0) {
        expressionNodes =
          create_expression(csound, currentArg,
                            previousArg == NULL ? current : previousArg,
                            currentArg->line, currentArg->locn, typeTable);
        // free discarded node
      } else {
        expressionNodes =
          create_boolean_expression(csound, currentArg,
                                    currentArg->line, currentArg->locn,
                                    typeTable);

      }
      if (expressionNodes == NULL) {
        // error
        return NULL;
      }

      nextArg = currentArg->next;
      csound->Free(csound, currentArg);

      /* Set as anchor if necessary */
      anchor = appendToTree(csound, anchor, expressionNodes);

      /* reconnect into chain */
      last = tree_tail(expressionNodes);
      newArg = last->left->value->lexeme;

      if (UNLIKELY(PARSER_DEBUG))
        csound->Message(csound, "New Arg: %s\n", newArg);

      /* handle arg replacement of currentArg here */
      /* **** was a bug as currentArg could be freed above **** */
      newArgTree = create_ans_token(csound, newArg);

      if (previousArg == NULL) {
        current->right = newArgTree;
      }
      else {
        previousArg->next = newArgTree;
      }

      newArgTree->next = nextArg;
      currentArg = nextArg;
    }

    previousArg = currentArg;
    currentArg = currentArg != NULL ? currentArg->next : NULL;
  }

  anchor = appendToTree(csound, anchor, current);

  // handle LHS expressions (i.e. array-set's)
  previousArg = NULL;
  currentArg = current->left;
  int init = 0;
  if (strcmp("init", current->value->lexeme)==0) {
    //print_tree(csound, "init",current);
    init = 1;
  }

  TREE* nextAnchor = NULL;
  TREE* nextLeft = NULL;
  while (currentArg != NULL) {

    if (currentArg->type == STRUCT_EXPR) {
        TREE* opcodeCallNode = create_opcode_token(csound, "##member_set");
        TREE* currentMember = currentArg->right;
        nextAnchor = appendToTree(csound, nextAnchor, opcodeCallNode);

        if (currentArg->left->type == STRUCT_EXPR) {
          TREE* structSubExpr = create_expression(
            csound,
            currentArg->left,
            currentArg,
            currentArg->line,
            currentArg->locn,
            typeTable
          );
          currentArg->left = NULL;
          opcodeCallNode->right = copy_node(
            csound, structSubExpr->left
          );
          structSubExpr->next = nextAnchor;
          nextAnchor = structSubExpr;
        } else if (currentArg->left->type == T_ARRAY) {
          TREE* arraySubExpr = create_expression(
            csound,
            currentArg->left,
            currentArg,
            currentArg->line,
            currentArg->locn,
            typeTable
          );
          currentArg->left = NULL;
          opcodeCallNode->right = copy_node(
            csound, arraySubExpr->left
          );
          arraySubExpr->next = nextAnchor;
          nextAnchor = arraySubExpr;
        } else if (nextLeft != NULL) {
          opcodeCallNode->right = nextLeft;
        } else {
          opcodeCallNode->right = currentArg->left;
        }

        if (currentMember->value->optype == NULL) {
          currentMember->value->optype = csound->Strdup(
            csound,
            opcodeCallNode->right->value->lexeme
          );
        }

        opcodeCallNode->right->next = currentMember;
        opcodeCallNode->right->next->next = current->right;

        // replacing T_ASSIGN '=' for member_set
        TREE* oldCurrent = current;
        current = opcodeCallNode;
        opcodeCallNode->next = oldCurrent->next;
        oldCurrent->left = NULL;
        oldCurrent->right = NULL;
        oldCurrent->next = NULL;
        if (anchor != NULL) {
          TREE* nextTail = anchor;
          while (nextTail != NULL && nextTail->next != oldCurrent) {
            nextTail = nextTail->next;
          }
          if (nextTail == NULL) {
            anchor = anchor->next;
          } else {
            nextTail->next = NULL;
          }
        }
        csound->Free(csound, oldCurrent);
    } else if (currentArg->type == T_ARRAY) {
      int isStructArray = currentArg->next != NULL &&
        currentArg->next->type == STRUCT_EXPR;
      int isArrayInStruct = currentArg->left->type == STRUCT_EXPR;

      if (isStructArray) {
        TREE* arrayIdent = create_synthetic_ident(csound, genlabs++);
        TREE* arrayGet = create_opcode_token(csound, "##array_get_struct");
        arrayGet->right = currentArg->left;
        arrayGet->right->next = currentArg->right;
        arrayGet->left = copy_node(csound, arrayIdent);

        nextLeft = arrayIdent;
        if (nextAnchor != NULL) {
          arrayGet->next = nextAnchor;
        }

        nextAnchor = arrayGet;

      } else {
        if (isArrayInStruct) {
          TREE* structSubExpr = create_expression(
            csound,
            currentArg->left,
            currentArg,
            currentArg->left->line,
            currentArg->left->locn,
            typeTable
          );
          structSubExpr->next = appendToTree(csound, structSubExpr->next, nextAnchor);
          nextAnchor = structSubExpr;
          currentArg->left = copy_node(csound, structSubExpr->left);
        }


        TREE* arraySet = create_opcode_token(
          csound, init ? "##array_init" : "##array_set"
        );
        arraySet->right = nextAnchor != NULL ?
          copy_node(csound, tree_tail(nextAnchor)->left) : currentArg->left;
        arraySet->right->next = current->right;
        arraySet->right->next->next = currentArg->right;

        nextAnchor = appendToTree(csound, nextAnchor, arraySet);
        // replacing T_ASSIGN '=/init' for array_set/init
        TREE* oldCurrent = current;
        current = arraySet;
        arraySet->next = oldCurrent->next;
        oldCurrent->left = NULL;
        oldCurrent->right = NULL;
        oldCurrent->next = NULL;
        if (anchor != NULL) {
          TREE* nextTail = anchor;
          while (nextTail != NULL && nextTail->next != oldCurrent) {
            nextTail = nextTail->next;
          }
          if (nextTail == NULL) {
            anchor = anchor->next;
          } else {
            nextTail->next = NULL;
          }
        }
        csound->Free(csound, oldCurrent);
      }

    }
    previousArg = currentArg;
    currentArg = currentArg->next;
  }

  if (nextLeft != NULL) {
    current->left = nextLeft;
  }


  if (nextAnchor != NULL) {
    anchor = appendToTree(csound, anchor, nextAnchor);
  }
  appendToTree(csound, anchor, originalNext);

  return anchor;
}

/* Flattens one level of if-blocks, sub-if-blocks should get flattened
   when the expander goes through statements */
TREE* expand_if_statement(CSOUND* csound,
                          TREE* current, TYPE_TABLE* typeTable) {

  TREE* anchor = NULL;
  TREE* expressionNodes = NULL;

  TREE* left = current->left;
  TREE* right = current->right;
  TREE* last;
  TREE* gotoToken;

  if (right->type == IGOTO_TOKEN ||
      right->type == KGOTO_TOKEN ||
      right->type == GOTO_TOKEN) {
    if (UNLIKELY(PARSER_DEBUG))
      csound->Message(csound, "Found if-goto\n");
    expressionNodes =
      create_boolean_expression(csound, left, right->line,
                                right->locn, typeTable);


    anchor = appendToTree(csound, anchor, expressionNodes);

    /* reconnect into chain */
    last = tree_tail(expressionNodes);

    gotoToken = create_goto_token(csound,
                                  last->left->value->lexeme,
                                  right,
                                  last->left->type == 'k' ||
                                  right->type =='k');
    last->next = gotoToken;
    gotoToken->next = current->next;
  }
  else if (LIKELY(right->type == THEN_TOKEN ||
                  right->type == ITHEN_TOKEN ||
                  right->type == KTHEN_TOKEN)) {
    int endLabelCounter = -1;
    TREE *tempLeft;
    TREE *tempRight;
    TREE* last;

    TREE *ifBlockCurrent = current;

    if (UNLIKELY(PARSER_DEBUG))
      csound->Message(csound, "Found if-then\n");
    if (right->next != NULL) {
      endLabelCounter = genlabs++;
    }

    while (ifBlockCurrent != NULL) {
      tempLeft = ifBlockCurrent->left;
      tempRight = ifBlockCurrent->right;

      if (ifBlockCurrent->type == ELSE_TOKEN) {
        appendToTree(csound, anchor, tempRight);
        break;
      }

      expressionNodes =
        create_boolean_expression(csound, tempLeft,
                                  tempLeft->line, tempLeft->locn,
                                  typeTable);

      anchor = appendToTree(csound, anchor, expressionNodes);

      last = tree_tail(expressionNodes);

      /* reconnect into chain */
      {
        TREE *statements, *label, *labelEnd, *gotoToken;
        int gotoType;

        statements = tempRight->right;
        label = create_synthetic_label_ident(csound, genlabs);
        labelEnd = create_synthetic_label(csound, genlabs++);
        tempRight->right = label;

        typeTable->labelList =
          cs_cons(csound,
                  cs_strdup(csound,
                            labelEnd->value->lexeme),
                  typeTable->labelList);
        //printf("allocate label %s\n", typeTable->labelList->value );

        gotoType = // checking for #B... var name
          (last->left->value->lexeme[1] == 'B');
        gotoToken =
          create_goto_token(csound,
                            last->left->value->lexeme,
                            tempRight,
                            gotoType);
        gotoToken->next = statements;
        anchor = appendToTree(csound, anchor, gotoToken);

        /* relinking */
        last = tree_tail(last);

        if (endLabelCounter > 0) {
          TREE *endLabel = create_synthetic_label_ident(
            csound,
            endLabelCounter
          );
          int type = (gotoType == 1) ? 0 : 2;
          /* csound->DebugMsg(csound, "%s(%d): type = %d %d\n", */
          /*        __FILE__, __LINE__, type, gotoType); */
          TREE *gotoEndLabelToken =
            create_simple_goto_token(csound, endLabel, type);
          if (UNLIKELY(PARSER_DEBUG))
            csound->Message(csound, "Creating simple goto token\n");

          appendToTree(csound, last, gotoEndLabelToken);

          gotoEndLabelToken->next = labelEnd;
        }
        else {
          appendToTree(csound, last, labelEnd);
        }

        ifBlockCurrent = tempRight->next;
      }
    }

    if (endLabelCounter > 0) {
      TREE *endLabel = create_synthetic_label(csound,
                                              endLabelCounter);
      anchor = appendToTree(csound, anchor, endLabel);

      typeTable->labelList = cs_cons(csound,
                                     cs_strdup(csound,
                                               endLabel->value->lexeme),
                                     typeTable->labelList);
      //printf("allocate label %s\n", typeTable->labelList->value );
    }

    anchor = appendToTree(csound, anchor, current->next);

  }
  else {
    csound->Message(csound,
                    Str("ERROR: Neither if-goto or if-then found on line %d!!!"),
                    right->line);
  }

  return anchor;
}

/* 1. create top label to loop back to
   2. do boolean expression
   3. do goto token that checks boolean and goes to end label
   4. insert statements
   5. add goto token that goes to top label
   6. end label */
TREE* expand_until_statement(CSOUND* csound, TREE* current,
                             TYPE_TABLE* typeTable, int dowhile)
{
  TREE* anchor = NULL;
  TREE* expressionNodes = NULL;

  TREE* gotoToken;

  int32 topLabelCounter = genlabs++;
  int32 endLabelCounter = genlabs++;
  TREE* tempRight = current->right;
  TREE* last = NULL;
  TREE* labelEnd;
  int gotoType;

  anchor = create_synthetic_label(csound, topLabelCounter);
  typeTable->labelList = cs_cons(csound,
                                 cs_strdup(csound, anchor->value->lexeme),
                                 typeTable->labelList);

  expressionNodes = create_boolean_expression(csound,
                                              current->left,
                                              current->line,
                                              current->locn,
                                              typeTable);
  anchor = appendToTree(csound, anchor, expressionNodes);
  last = tree_tail(anchor);

  labelEnd = create_synthetic_label(csound, endLabelCounter);
  typeTable->labelList = cs_cons(csound,
                                 cs_strdup(csound, labelEnd->value->lexeme),
                                 typeTable->labelList);

  gotoType =
    last->left->value->lexeme[1] == 'B'; // checking for #B... var name

  // printf("%s\n", last->left->value->lexeme);
  //printf("gottype = %d ; dowhile = %d\n", gotoType, dowhile);
  gotoToken =
    create_goto_token(csound,
                      last->left->value->lexeme,
                      labelEnd,
                      gotoType+0x8000*dowhile);
  gotoToken->next = tempRight;
  gotoToken->right->next = labelEnd;


  last = appendToTree(csound, last, gotoToken);
  last = tree_tail(last);


  labelEnd = create_synthetic_label(csound, endLabelCounter);
  TREE *topLabel = create_synthetic_label_ident(
    csound,
    topLabelCounter
  );
  TREE *gotoTopLabelToken = create_simple_goto_token(csound,
                                                     topLabel,
                                                     (gotoType==1 ? 0 : 1));

  appendToTree(csound, last, gotoTopLabelToken);
  gotoTopLabelToken->next = labelEnd;


  labelEnd->next = current->next;
  return anchor;
}

TREE* expand_for_statement(
  CSOUND* csound,
  TREE* current,
  TYPE_TABLE* typeTable,
  char* arrayArgType
) {

  CS_TYPE *iType = (CS_TYPE *)&CS_VAR_TYPE_I;
  CS_TYPE *kType = (CS_TYPE *)&CS_VAR_TYPE_K;

  int isPerfRate = arrayArgType[1] == 'k';
  char* op = (char *)csound->Malloc(csound, 10);
  // create index counter
  TREE *indexAssign = create_empty_token(csound);
  indexAssign->value = make_token(csound, "=");
  indexAssign->type = T_ASSIGNMENT;
  indexAssign->value->type = T_ASSIGNMENT;
  char *indexName = create_synthetic_var_name(
    csound,
    genlabs++,
    isPerfRate ? 'k' : 'i'
  );
  TREE *indexIdent = create_empty_token(csound);
  indexIdent->value = make_token(csound, indexName);
  indexIdent->type = T_IDENT;
  indexIdent->value->type = T_IDENT;
  TREE *zeroToken = create_empty_token(csound);
  zeroToken->value = make_token(csound, "0");
  zeroToken->value->value = 0;
  zeroToken->type = INTEGER_TOKEN;
  zeroToken->value->type = INTEGER_TOKEN;
  indexAssign->left = indexIdent;
  indexAssign->right = zeroToken;

  TREE *arrayAssign = create_empty_token(csound);
  arrayAssign->value = make_token(csound, "=");
  arrayAssign->type = T_ASSIGNMENT;
  arrayAssign->value->type = T_ASSIGNMENT;
  char *arrayName = create_synthetic_array_var_name(
    csound,
    genlabs++,
    isPerfRate ? 'k' : 'i'
  );
  TREE *arrayIdent = create_empty_token(csound);
  arrayIdent->value = make_token(csound, arrayName);
  arrayIdent->type = T_ARRAY_IDENT;
  arrayIdent->value->type = T_ARRAY_IDENT;
  add_array_arg(csound, arrayName, NULL, 1, typeTable);

  arrayAssign->left = arrayIdent;
  arrayAssign->right = current->right->left;
  indexAssign->next = arrayAssign;

  TREE *arrayLength = create_empty_token(csound);
  arrayLength->value = make_token(csound, "=");
  arrayLength->type = T_ASSIGNMENT;
  arrayLength->value->type = T_ASSIGNMENT;
  char *arrayLengthName = create_synthetic_var_name(
    csound,
    genlabs++,
    isPerfRate ? 'k' : 'i'
  );
  TREE *arrayLengthIdent = create_empty_token(csound);
  arrayLengthIdent->value = make_token(csound, arrayLengthName);
  arrayLengthIdent->type = T_IDENT;
  arrayLengthIdent->value->type = T_IDENT;
  arrayLength->left = arrayLengthIdent;
  TREE *arrayLengthFn = create_empty_token(csound);
  arrayLengthFn->value = make_token(csound, "lenarray");
  arrayLengthFn->type = T_FUNCTION;
  arrayLengthFn->value->type = T_FUNCTION;
  TREE *arrayLengthArrayIdent = copy_node(csound, arrayIdent);
  arrayLengthFn->right = arrayLengthArrayIdent;
  arrayLength->right = arrayLengthFn;
  arrayAssign->next = arrayLength;


  TREE* loopLabel = create_synthetic_label(csound, genlabs++);
  loopLabel->type = LABEL_TOKEN;
  loopLabel->value->type = LABEL_TOKEN;
  CS_VARIABLE *loopLabelVar = csoundCreateVariable(
      csound, csound->typePool, isPerfRate ? kType : iType,
      loopLabel->value->lexeme, 0, NULL);
  loopLabelVar->varType = (CS_TYPE *)&CS_VAR_TYPE_L;
  csoundAddVariable(csound, typeTable->localPool, loopLabelVar);
  typeTable->labelList = cs_cons(csound,
                                 cs_strdup(csound, loopLabel->value->lexeme),
                                 typeTable->labelList);

  arrayLength->next = loopLabel;

  // handle case where user provided an index identifier
  int hasOptionalIndex = 0;
  if (current->left->next != NULL) {
    hasOptionalIndex = 1;
    TREE *optionalUserIndexAssign = create_empty_token(csound);
    optionalUserIndexAssign->value = make_token(csound, "=");
    optionalUserIndexAssign->type = T_ASSIGNMENT;
    optionalUserIndexAssign->value->type = T_ASSIGNMENT;
    optionalUserIndexAssign->left = current->left->next;
    optionalUserIndexAssign->right = copy_node(csound, indexIdent);
    current->left->next = NULL;
    loopLabel->next = optionalUserIndexAssign;
  }

  TREE* arrayGetStatement = create_opcode_token(csound, "##array_get");
  arrayGetStatement->left = current->left;
  arrayGetStatement->right = copy_node(csound, arrayIdent);
  arrayGetStatement->right->next = copy_node(csound, indexIdent);
  if (hasOptionalIndex) {
    loopLabel->next->next = arrayGetStatement;
  } else {
    loopLabel->next = arrayGetStatement;
  }
  arrayGetStatement->next = current->right->right;

  strNcpy(op, isPerfRate ? "loop_lt.k" : "loop_lt.i", 10);

  TREE* loopLtStatement = create_opcode_token(csound, op);
  TREE* tail = tree_tail(current->right->right);
  tail->next = loopLtStatement;

  TREE* indexArgToken = copy_node(csound, indexIdent);
  loopLtStatement->right = indexArgToken;

  // loop less-than arg1: increment by 1
  TREE *oneToken = create_empty_token(csound);
  oneToken->value = make_token(csound, "1");
  oneToken->value->value = 1;
  oneToken->type = INTEGER_TOKEN;
  oneToken->value->type = INTEGER_TOKEN;
  indexArgToken->next = oneToken;

  // loop less-than arg2: max iterations (length of the array)
  TREE* arrayLengthArgToken = copy_node(csound, arrayLengthIdent);

  oneToken->next = arrayLengthArgToken;

  // loop less-than arg3: goto label
  TREE *labelGotoIdent = create_empty_token(csound);
  labelGotoIdent->value = make_token(csound, loopLabel->value->lexeme);
  labelGotoIdent->type = T_IDENT;
  labelGotoIdent->value->type = T_IDENT;
  arrayLengthArgToken->next = labelGotoIdent;


  csound->Free(csound, indexName);
  csound->Free(csound, arrayName);
  csound->Free(csound, arrayLengthName);
  csound->Free(csound, op);

  return indexAssign;
}

int is_statement_expansion_required(TREE* root) {
  TREE* current = root->right;
  while (current != NULL) {
    if (is_boolean_expression_node(current) || is_expression_node(current)) {
      return 1;
    }
    current = current->next;
  }

  /*  VL: do we  need  to always expand  ARRAY expressions?
      would this lead to unecessary copying at times?
   */
  current = root->left;
  while (current != NULL) {
    if (current->type == T_ARRAY || current->type == STRUCT_EXPR) {
      return 1;
    }
    current = current->next;
  }
  return 0;
}
