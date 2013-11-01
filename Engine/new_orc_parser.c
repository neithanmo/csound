/*
    new_orc_parser.c:

    Copyright (C) 2006
    Steven Yi
    Modifications 2009 by Christopher Wilson for multicore

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
    02111-1307 USA
*/

#include "csoundCore.h"
#include "csound_orc.h"
#include "corfile.h"

extern void csound_orcrestart(FILE*, void *);

extern int csound_orcdebug;

extern void print_csound_predata(void *);
extern void csound_prelex_init(void *);
extern void csound_preset_extra(void *, void *);

extern void csound_prelex(CSOUND*, void*);
extern void csound_prelex_destroy(void *);

extern void csound_orc_scan_buffer (const char *, size_t, void*);
extern int csound_orcparse(PARSE_PARM *, void *, CSOUND*, TREE*);
extern void csound_orclex_init(void *);
extern void csound_orcset_extra(void *, void *);
extern void csound_orcset_lineno(int, void*);
extern void csound_orclex_destroy(void *);
extern void init_symbtab(CSOUND*);
extern void print_tree(CSOUND *, char *, TREE *);
extern TREE* verify_tree(CSOUND *, TREE *, TYPE_TABLE*);
extern TREE *csound_orc_expand_expressions(CSOUND *, TREE *);
extern TREE* csound_orc_optimize(CSOUND *, TREE *);
extern void csp_orc_analyze_tree(CSOUND* csound, TREE* root);


void csound_print_preextra(CSOUND *csound, PRE_PARM  *x)
{
    csound->DebugMsg(csound,"********* Extra Pre Data %p *********\n", x);
    csound->DebugMsg(csound,"macros = %p, macro_stack_ptr = %u, ifdefStack=%p,\n"
           "isIfndef=%d\n, line=%d\n",
           x->macros, x->macro_stack_ptr, x->ifdefStack, x->isIfndef, x->line);
    csound->DebugMsg(csound,"******************\n");
}

uint32_t make_location(PRE_PARM *qq)
{
    int d = qq->depth;
    uint32_t loc = 0;
    int n = (d>6?d-5:0);
    for (; n<=d; n++) {
      loc = (loc<<6)+(qq->lstack[n]);
    }
    return loc;
}

TREE *csoundParseOrc(CSOUND *csound, const char *str)
{
    int err;
    OPARMS *O = csound->oparms;
    {
      PRE_PARM    qq;
      /* Preprocess */
      memset(&qq, 0, sizeof(PRE_PARM));
      csound_prelex_init(&qq.yyscanner);
      csound_preset_extra(&qq, qq.yyscanner);
      qq.line = csound->orcLineOffset;
      csound->expanded_orc = corfile_create_w();
      file_to_int(csound, "**unknown**");
      if (str == NULL) {
        char bb[80];

        if (csound->orchstr==NULL && !csound->oparms->daemon)
          csound->Die(csound,
                      Str("Failed to open input file %s\n"), csound->orchname);
        else return NULL;

        if (csound->orchname==NULL ||
            csound->orchname[0]=='\0') csound->orchname = csound->csdname;
        /* We know this is the start so stack is empty so far */
        sprintf(bb, "#source %d\n",
                qq.lstack[0] = file_to_int(csound, csound->orchname));
        corfile_puts(bb, csound->expanded_orc);
        sprintf(bb, "#line %d\n", csound->orcLineOffset);
        corfile_puts(bb, csound->expanded_orc);
      }
      else {
        if (csound->orchstr == NULL ||
            corfile_body(csound->orchstr) == NULL)
          csound->orchstr = corfile_create_w();
        else
          corfile_reset(csound->orchstr);
        corfile_puts(str, csound->orchstr);
        corfile_puts("\n#exit\n", csound->orchstr);
        corfile_putc('\0', csound->orchstr);
        corfile_putc('\0', csound->orchstr);
      }
      csound->DebugMsg(csound, "Calling preprocess on >>%s<<\n",
              corfile_body(csound->orchstr));
      //csound->DebugMsg(csound,"FILE: %s \n", csound->orchstr->body);
      //    csound_print_preextra(&qq);
      cs_init_math_constants_macros(csound, &qq);
      cs_init_omacros(csound, &qq, csound->omacros);
      //    csound_print_preextra(&qq);
      csound_prelex(csound, qq.yyscanner);
      if (UNLIKELY(qq.ifdefStack != NULL)) {
        csound->Message(csound, Str("Unmatched #ifdef\n"));
        csound->LongJmp(csound, 1);
      }
      csound_prelex_destroy(qq.yyscanner);
      csound->DebugMsg(csound, "yielding >>%s<<\n",
                       corfile_body(csound->expanded_orc));
      corfile_rm(&csound->orchstr);
    }
    {
      TREE* astTree = (TREE *)mcalloc(csound, sizeof(TREE));
      TREE* newRoot;
      PARSE_PARM  pp;
      TYPE_TABLE* typeTable = NULL;

      /* Parse */
      memset(&pp, '\0', sizeof(PARSE_PARM));

      init_symbtab(csound);

      csound_orcdebug = O->odebug;
      csound_orclex_init(&pp.yyscanner);

      csound_orcset_extra(&pp, pp.yyscanner);
      csound_orc_scan_buffer(corfile_body(csound->expanded_orc),
                             corfile_tell(csound->expanded_orc), pp.yyscanner);
      //csound_orcset_lineno(csound->orcLineOffset, pp.yyscanner);
      err = csound_orcparse(&pp, pp.yyscanner, csound, astTree);
      corfile_rm(&csound->expanded_orc);
      if (csound->synterrcnt) err = 3;
      if (LIKELY(err == 0)) {
        if(csound->oparms->odebug) csound->Message(csound, "Parsing successful!\n");
      }
      else {
        if (err == 1){
          csound->Message(csound, Str("Parsing failed due to invalid input!\n"));
        }
        else if (err == 2){
          csound->Message(csound,
                          Str("Parsing failed due to memory exhaustion!\n"));
        }
        else if (err == 3){
          csound->Message(csound, Str("Parsing failed due to %d syntax error%s!\n"),
                          csound->synterrcnt, csound->synterrcnt==1?"":"s");
        }
        goto ending;
      }
      if (UNLIKELY(PARSER_DEBUG)) {
        print_tree(csound, "AST - INITIAL\n", astTree);
      }
      //print_tree(csound, "AST - INITIAL\n", astTree);
      typeTable = mmalloc(csound, sizeof(TYPE_TABLE));
      typeTable->udos = NULL;

      typeTable->globalPool = mcalloc(csound, sizeof(CS_VAR_POOL));
      typeTable->instr0LocalPool = mcalloc(csound, sizeof(CS_VAR_POOL));

      typeTable->localPool = typeTable->instr0LocalPool;
      typeTable->labelList = NULL;

      /**** THIS NEXT LINE IS WRONG AS err IS int WHILE FN RETURNS TREE* ****/
      astTree = verify_tree(csound, astTree, typeTable);
//      mfree(csound, typeTable->instr0LocalPool);
//      mfree(csound, typeTable->globalPool);
//      mfree(csound, typeTable);
      //print_tree(csound, "AST - FOLDED\n", astTree);

      //FIXME - synterrcnt should not be global
      if (astTree == NULL || csound->synterrcnt){
          err = 3;
          csound->Message(csound, "Parsing failed due to %d semantic error%s!\n",
                          csound->synterrcnt, csound->synterrcnt==1?"":"s");
          goto ending;
      }
      err = 0;

      //csp_orc_analyze_tree(csound, astTree);

//      astTree = csound_orc_expand_expressions(csound, astTree);
//
      if (UNLIKELY(PARSER_DEBUG)) {
        print_tree(csound, "AST - AFTER VERIFICATION/EXPANSION\n", astTree);
      }

    ending:
      csound_orclex_destroy(pp.yyscanner);
      if(err) {
        csound->Warning(csound, Str("Stopping on parser failure\n"));
        csoundDeleteTree(csound, astTree);
        if (typeTable != NULL) {
          mfree(csound, typeTable);
        }
        return NULL;
      }

      astTree = csound_orc_optimize(csound, astTree);

      // small hack: use an extra node as head of tree list to hold the
      // typeTable, to be used during compilation
      newRoot = make_leaf(csound, 0, 0, 0, NULL);
      newRoot->markup = typeTable;
      newRoot->next = astTree;


      return newRoot;
    }
}
