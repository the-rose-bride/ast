#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

int readFileIntoBuffer(const char *file, uint8_t *buffer, size_t bufferLen)
{
  FILE *fp = fopen(file, "r");
  if (NULL == fp) return 1;
  //printf("file opened\n");
  size_t bytesRead = fread(buffer, 1, bufferLen - 1, fp);
  if (bytesRead == 0)
  {
    // could be error or end of file, don't care
    return 1;
  }
  //printf("file read\n");
  buffer[bytesRead] = '\n'; // newline at end of file
  buffer[bytesRead+1] = '\0'; // null terminate buffer
  fclose(fp);
  return 0; // success
}

static bool bufferGetNextLine(char **inputBuffer, char *lineBuffer, size_t lineBufferLength)
{
  char *newlinePosition = strchr(*inputBuffer, '\n');
  if (0 >= newlinePosition)
  {
    *lineBuffer = 0;
    return false;
  }
    
  const size_t c_lineLength = (newlinePosition - *inputBuffer);
  memcpy(lineBuffer, *inputBuffer, c_lineLength);
  lineBuffer[c_lineLength] = '\0';
  *inputBuffer += c_lineLength + 1;
  return true;
}

typedef enum _astCommand {
  E_None, E_Hello, E_Block, E_Statement
} ASTCommand;

const char * astCommandString(ASTCommand cmd)
{
  if (cmd == E_None) return "None";
  if (cmd == E_Hello) return "HELLO";
  if (cmd == E_Block) return "Block";
  if (cmd == E_Statement) return "Statement";
  return "Unknown AST Command";
}

typedef struct _astNode ASTNode;
typedef struct _astNode
{
  ASTCommand cmd;
  ASTNode *next;
  ASTNode *child; // Only for 'block' nodes
} ASTNode;

typedef struct _astTree
{
  ASTNode *top;
  int numberNodes;
  int maxDepth;
} ASTTree;

void astTreeInit(ASTTree *pTree)
{
  pTree->top = NULL;
  pTree->numberNodes = 0;
  pTree->maxDepth = 0;
}

#define c_ast_stack_size 1024

typedef struct _astIter
{
  ASTTree *pTree;
  ASTNode *pCurrent;
  // Iter stack
  ASTNode *buffer[c_ast_stack_size];
  ASTNode **stack;
  int stackCount;
} ASTIter;

void astIterInit(ASTIter *pIter, ASTTree *pTree)
{
  pIter->pTree = pTree;
  pIter->pCurrent = NULL; // Null is the start of the tree
  pIter->stack = pIter->buffer;
  pIter->stackCount = 0;
  memset(pIter->buffer, 0, c_ast_stack_size * sizeof(ASTNode*));
}

ASTNode *astMakeNode()
{
  ASTNode *newNode = (ASTNode*)malloc(sizeof(ASTNode));
  //printf("New node [0x%X]\n", (uint16_t)newNode);
  newNode->cmd = E_None;
  newNode->next = NULL;
  newNode->child = NULL;
  return newNode;
}

void push(ASTNode ***stack, ASTNode *value)
{
  **stack = value;
  (*stack)++;
}

ASTNode *pop(ASTNode ***stack)
{
  (*stack)--;
  return **stack;
}

ASTNode *stack_top(ASTNode ***stack)
{
  return *((*stack) - 1);
}

void print_stack(ASTIter *pIter)
{
  for (int i = 0; i < pIter->stackCount; ++i)
  {
    printf("stack[%i] %p -> child:%p, next:%p\n", i,
	   pIter->buffer[i],
	   pIter->buffer[i]->child,
	   pIter->buffer[i]->next);
  }
}

//#define PARSE_LOGGING 1
#ifdef PARSE_LOGGING
const bool c_parseLogging = true;
#define parseLog(fmt, ...) printf(fmt, __VA_ARGS__)
#else
const bool c_parseLogging = false;
#define parseLog
#endif // PARSE_LOGGIN

void create_next_node(ASTIter *pIter)
{
  // Handle empty tree and open blocks
  if (pIter->pCurrent == NULL)
  {
    // If the stack is not empty, this is an open block - add to it
    if (pIter->stackCount)
    {
      pIter->pCurrent = astMakeNode();
      ASTNode *pStackTop = stack_top(&pIter->stack);
      pStackTop->child = pIter->pCurrent;
      parseLog("{%d} create_next_node() stack_top->child = %p->%p\n",
	     pIter->stackCount,
	     pStackTop,
	     pIter->pCurrent);
      return;
    }
    // Otherwise initialise the tree
    pIter->pTree->top = astMakeNode();
    pIter->pCurrent = pIter->pTree->top;
    parseLog("{%d} create_next_node() start tree = %p\n",
	   pIter->stackCount,
	   pIter->pCurrent);
    return;
  }

  pIter->pCurrent->next = astMakeNode();
  pIter->pCurrent = pIter->pCurrent->next;
  parseLog("{%d} create_next_node() next = %p\n",
	 pIter->stackCount,
	 pIter->pCurrent);
}

void create_block_node(ASTIter *pIter)
{
  create_next_node(pIter);
  parseLog("create_block_node() %p\n", pIter->pCurrent);
  pIter->pCurrent->cmd = E_Block;
  /* Push this new block node to the stack; we return to it later */
  if (pIter->stackCount >= c_ast_stack_size) return;
  push(&pIter->stack, pIter->pCurrent);
  ++pIter->stackCount;
  
  pIter->pCurrent = NULL; // NULL indicates open block node; stack will be checked when adding next node
}

void close_block_node(ASTIter *pIter)
{
  if (!pIter->stackCount) return;  /* no block to close here */
  pIter->pCurrent = pop(&pIter->stack); /* if current is not null, the block node is closed */
  parseLog("close_block_node() current->child %p->%p\n", pIter->pCurrent, pIter->pCurrent->child);
  --pIter->stackCount;
}

bool iter_get_next(ASTIter *pIter)
{
  if (pIter->pCurrent == NULL) 	   /* Current is only null at start of iterating */
  {
    pIter->pCurrent = pIter->pTree->top;
  }
  else if (NULL != pIter->pCurrent->child) /* If there is a child node we must visit it */
  {
    if (pIter->stackCount >= c_ast_stack_size) return false; /* stack overrun, meh */
    push(&pIter->stack, pIter->pCurrent);
    pIter->stackCount++;
    pIter->pCurrent = pIter->pCurrent->child;
  }
  else if (NULL != pIter->pCurrent->next) /* If there is a next node, visit that */
  {
    pIter->pCurrent = pIter->pCurrent->next;
  }
  else if (pIter->stackCount)    /* No child or next node? Go up the stack */
  {
    pIter->pCurrent = pop(&pIter->stack)->next; /* Have to go straight to next node up one level */
    pIter->stackCount--;
  }
  else
  {
    pIter->pCurrent = NULL; 	   /* We have reached the end, there is no next, child, or stack to peruse */
  }
  return (pIter->pCurrent != NULL);
}

void printAST(ASTTree *pTree)
{
  ASTIter iter;
  astIterInit(&iter, pTree);

  //printf("[%X] %s [next=[0x%X],child=[0x%X]]\n", (uint16_t)current, astCommandString(current->cmd), (uint16_t)current->next, (uint16_t)current->child);
  
  while (iter_get_next(&iter))
  {
    //printf("{%d} %s [%p]\n", iter.stackCount, astCommandString(iter.pCurrent->cmd), iter.pCurrent);
    printf("{%d} %s\n", iter.stackCount, astCommandString(iter.pCurrent->cmd));
    //print_stack(&iter);
  }
}

static bool lineMatches(char *line, const char *match)
{
  return (0 == strncasecmp(match, line, strlen(match)));
}

void parseLineIntoAST(ASTIter *pIter, char *lineBuf)
{
  // remove leading whitespace
  while (*lineBuf == ' ') ++lineBuf;
  
  if (lineMatches(lineBuf, "start"))
  {
    create_block_node(pIter);
    //printf("made block node\n");
  }
  else if (lineMatches(lineBuf, "end"))
  {
    close_block_node(pIter);
    //printf("closed block node\n");
  }
  else if (lineMatches(lineBuf, "hello"))
  {
    create_next_node(pIter);
    pIter->pCurrent->cmd = E_Hello;
    //printf("made HELLO node\n");
  }
  else if (0 < strlen(lineBuf)) // Failover to statement if not a block
  {
    create_next_node(pIter);
    pIter->pCurrent->cmd = E_Statement;
    //printf("made statement node\n");
  }

  if (c_parseLogging) print_stack(pIter);
}

int main(int argc, char **argv)
{
  // Read into buffer
  char buf[256];
  readFileIntoBuffer("./input", buf, sizeof(buf));

  // Scan the file contents buffer into line buffers
  const int c_lineBufferSize = 100;
  const int c_lineBufferLineLength = 128;
  char lines[c_lineBufferSize][c_lineBufferLineLength];
  int numberOfLines = 0;
  char *pBuf = buf;
  while (bufferGetNextLine(&pBuf, lines[numberOfLines++], c_lineBufferLineLength));

  // Parse the lines into AST tree
  ASTTree tree;
  astTreeInit(&tree);
  ASTIter iter;
  astIterInit(&iter, &tree);
  for (size_t i = 0; i < numberOfLines; ++i)
  {
    char *lineBuf = lines[i];
    //printf("%zu: \"%s\"\n", i, lineBuf);
    parseLineIntoAST(&iter, lineBuf);
  }
  //printf("\n");

  // Print AST structure
  printf("Print AST:\n");
  printAST(&tree);
  return 0;
}
