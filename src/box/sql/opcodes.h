/* Automatically generated.  Do not edit */
/* See the tool/mkopcodeh.tcl script for details */
#define OP_Savepoint       0
#define OP_AutoCommit      1
#define OP_Transaction     2
#define OP_SorterNext      3
#define OP_PrevIfOpen      4
#define OP_Or              5 /* same as TK_OR, synopsis: r[P3]=(r[P1] || r[P2]) */
#define OP_And             6 /* same as TK_AND, synopsis: r[P3]=(r[P1] && r[P2]) */
#define OP_Not             7 /* same as TK_NOT, synopsis: r[P2]= !r[P1]    */
#define OP_NextIfOpen      8
#define OP_Prev            9
#define OP_Next           10
#define OP_Checkpoint     11
#define OP_JournalMode    12
#define OP_IsNull         13 /* same as TK_ISNULL, synopsis: if r[P1]==NULL goto P2 */
#define OP_NotNull        14 /* same as TK_NOTNULL, synopsis: if r[P1]!=NULL goto P2 */
#define OP_Ne             15 /* same as TK_NE, synopsis: IF r[P3]!=r[P1]   */
#define OP_Eq             16 /* same as TK_EQ, synopsis: IF r[P3]==r[P1]   */
#define OP_Gt             17 /* same as TK_GT, synopsis: IF r[P3]>r[P1]    */
#define OP_Le             18 /* same as TK_LE, synopsis: IF r[P3]<=r[P1]   */
#define OP_Lt             19 /* same as TK_LT, synopsis: IF r[P3]<r[P1]    */
#define OP_Ge             20 /* same as TK_GE, synopsis: IF r[P3]>=r[P1]   */
#define OP_ElseNotEq      21 /* same as TK_ESCAPE                          */
#define OP_BitAnd         22 /* same as TK_BITAND, synopsis: r[P3]=r[P1]&r[P2] */
#define OP_BitOr          23 /* same as TK_BITOR, synopsis: r[P3]=r[P1]|r[P2] */
#define OP_ShiftLeft      24 /* same as TK_LSHIFT, synopsis: r[P3]=r[P2]<<r[P1] */
#define OP_ShiftRight     25 /* same as TK_RSHIFT, synopsis: r[P3]=r[P2]>>r[P1] */
#define OP_Add            26 /* same as TK_PLUS, synopsis: r[P3]=r[P1]+r[P2] */
#define OP_Subtract       27 /* same as TK_MINUS, synopsis: r[P3]=r[P2]-r[P1] */
#define OP_Multiply       28 /* same as TK_STAR, synopsis: r[P3]=r[P1]*r[P2] */
#define OP_Divide         29 /* same as TK_SLASH, synopsis: r[P3]=r[P2]/r[P1] */
#define OP_Remainder      30 /* same as TK_REM, synopsis: r[P3]=r[P2]%r[P1] */
#define OP_Concat         31 /* same as TK_CONCAT, synopsis: r[P3]=r[P2]+r[P1] */
#define OP_Goto           32
#define OP_BitNot         33 /* same as TK_BITNOT, synopsis: r[P1]= ~r[P1] */
#define OP_Gosub          34
#define OP_InitCoroutine  35
#define OP_Yield          36
#define OP_MustBeInt      37
#define OP_Jump           38
#define OP_Once           39
#define OP_If             40
#define OP_IfNot          41
#define OP_SeekLT         42 /* synopsis: key=r[P3@P4]                     */
#define OP_SeekLE         43 /* synopsis: key=r[P3@P4]                     */
#define OP_SeekGE         44 /* synopsis: key=r[P3@P4]                     */
#define OP_SeekGT         45 /* synopsis: key=r[P3@P4]                     */
#define OP_NoConflict     46 /* synopsis: key=r[P3@P4]                     */
#define OP_NotFound       47 /* synopsis: key=r[P3@P4]                     */
#define OP_Found          48 /* synopsis: key=r[P3@P4]                     */
#define OP_SeekRowid      49 /* synopsis: intkey=r[P3]                     */
#define OP_NotExists      50 /* synopsis: intkey=r[P3]                     */
#define OP_Last           51
#define OP_SorterSort     52
#define OP_Sort           53
#define OP_Rewind         54
#define OP_IdxLE          55 /* synopsis: key=r[P3@P4]                     */
#define OP_IdxGT          56 /* synopsis: key=r[P3@P4]                     */
#define OP_IdxLT          57 /* synopsis: key=r[P3@P4]                     */
#define OP_IdxGE          58 /* synopsis: key=r[P3@P4]                     */
#define OP_RowSetRead     59 /* synopsis: r[P3]=rowset(P1)                 */
#define OP_RowSetTest     60 /* synopsis: if r[P3] in rowset(P1) goto P2   */
#define OP_Program        61
#define OP_FkIfZero       62 /* synopsis: if fkctr[P1]==0 goto P2          */
#define OP_IfPos          63 /* synopsis: if r[P1]>0 then r[P1]-=P3, goto P2 */
#define OP_IfNotZero      64 /* synopsis: if r[P1]!=0 then r[P1]--, goto P2 */
#define OP_DecrJumpZero   65 /* synopsis: if (--r[P1])==0 goto P2          */
#define OP_Init           66 /* synopsis: Start at P2                      */
#define OP_Return         67
#define OP_EndCoroutine   68
#define OP_HaltIfNull     69 /* synopsis: if r[P3]=null halt               */
#define OP_Halt           70
#define OP_Integer        71 /* synopsis: r[P2]=P1                         */
#define OP_Bool           72 /* synopsis: r[P2]=P1                         */
#define OP_Int64          73 /* synopsis: r[P2]=P4                         */
#define OP_String         74 /* synopsis: r[P2]='P4' (len=P1)              */
#define OP_Null           75 /* synopsis: r[P2..P3]=NULL                   */
#define OP_String8        76 /* same as TK_STRING, synopsis: r[P2]='P4'    */
#define OP_SoftNull       77 /* synopsis: r[P1]=NULL                       */
#define OP_Blob           78 /* synopsis: r[P2]=P4 (len=P1, subtype=P3)    */
#define OP_Variable       79 /* synopsis: r[P2]=parameter(P1,P4)           */
#define OP_Move           80 /* synopsis: r[P2@P3]=r[P1@P3]                */
#define OP_Copy           81 /* synopsis: r[P2@P3+1]=r[P1@P3+1]            */
#define OP_SCopy          82 /* synopsis: r[P2]=r[P1]                      */
#define OP_IntCopy        83 /* synopsis: r[P2]=r[P1]                      */
#define OP_ResultRow      84 /* synopsis: output=r[P1@P2]                  */
#define OP_CollSeq        85
#define OP_Function0      86 /* synopsis: r[P3]=func(r[P2@P5])             */
#define OP_Function       87 /* synopsis: r[P3]=func(r[P2@P5])             */
#define OP_AddImm         88 /* synopsis: r[P1]=r[P1]+P2                   */
#define OP_RealAffinity   89
#define OP_Cast           90 /* synopsis: affinity(r[P1])                  */
#define OP_Permutation    91
#define OP_Compare        92 /* synopsis: r[P1@P3] <-> r[P2@P3]            */
#define OP_Column         93 /* synopsis: r[P3]=PX                         */
#define OP_Affinity       94 /* synopsis: affinity(r[P1@P2])               */
#define OP_MakeRecord     95 /* synopsis: r[P3]=mkrec(r[P1@P2])            */
#define OP_Count          96 /* synopsis: r[P2]=count()                    */
#define OP_TTransaction   97
#define OP_ReadCookie     98
#define OP_SetCookie      99
#define OP_ReopenIdx     100 /* synopsis: root=P2 iDb=P3                   */
#define OP_OpenRead      101 /* synopsis: root=P2 iDb=P3                   */
#define OP_OpenWrite     102 /* synopsis: root=P2 iDb=P3                   */
#define OP_OpenAutoindex 103 /* synopsis: nColumn=P2                       */
#define OP_OpenEphemeral 104 /* synopsis: nColumn=P2                       */
#define OP_SorterOpen    105
#define OP_SequenceTest  106 /* synopsis: if (cursor[P1].ctr++) pc = P2    */
#define OP_OpenPseudo    107 /* synopsis: P3 columns in r[P2]              */
#define OP_Close         108
#define OP_ColumnsUsed   109
#define OP_Sequence      110 /* synopsis: r[P2]=cursor[P1].ctr++           */
#define OP_NextId        111 /* synopsis: r[P3]=get_max(space_index[P1]{Column[P2]}) */
#define OP_FCopy         112 /* synopsis: reg[P2@cur_frame]= reg[P1@root_frame(OPFLAG_SAME_FRAME)] */
#define OP_NewRowid      113 /* synopsis: r[P2]=rowid                      */
#define OP_Insert        114 /* synopsis: intkey=r[P3] data=r[P2]          */
#define OP_InsertInt     115 /* synopsis: intkey=P3 data=r[P2]             */
#define OP_Real          116 /* same as TK_FLOAT, synopsis: r[P2]=P4       */
#define OP_Delete        117
#define OP_ResetCount    118
#define OP_SorterCompare 119 /* synopsis: if key(P1)!=trim(r[P3],P4) goto P2 */
#define OP_SorterData    120 /* synopsis: r[P2]=data                       */
#define OP_RowData       121 /* synopsis: r[P2]=data                       */
#define OP_Rowid         122 /* synopsis: r[P2]=rowid                      */
#define OP_NullRow       123
#define OP_SorterInsert  124 /* synopsis: key=r[P2]                        */
#define OP_IdxInsert     125 /* synopsis: key=r[P2]                        */
#define OP_IdxDelete     126 /* synopsis: key=r[P2@P3]                     */
#define OP_Seek          127 /* synopsis: Move P3 to P1.rowid              */
#define OP_IdxRowid      128 /* synopsis: r[P2]=rowid                      */
#define OP_Destroy       129
#define OP_Clear         130
#define OP_ResetSorter   131
#define OP_CreateIndex   132 /* synopsis: r[P2]=root iDb=P1                */
#define OP_CreateTable   133 /* synopsis: r[P2]=root iDb=P1                */
#define OP_ParseSchema   134
#define OP_ParseSchema2  135 /* synopsis: rows=r[P1@P2] iDb=P3             */
#define OP_ParseSchema3  136 /* synopsis: name=r[P1] sql=r[P1+1] iDb=P2    */
#define OP_LoadAnalysis  137
#define OP_DropTable     138
#define OP_DropIndex     139
#define OP_DropTrigger   140
#define OP_IntegrityCk   141
#define OP_RowSetAdd     142 /* synopsis: rowset(P1)=r[P2]                 */
#define OP_Param         143
#define OP_FkCounter     144 /* synopsis: fkctr[P1]+=P2                    */
#define OP_MemMax        145 /* synopsis: r[P1]=max(r[P1],r[P2])           */
#define OP_OffsetLimit   146 /* synopsis: if r[P1]>0 then r[P2]=r[P1]+max(0,r[P3]) else r[P2]=(-1) */
#define OP_AggStep0      147 /* synopsis: accum=r[P3] step(r[P2@P5])       */
#define OP_AggStep       148 /* synopsis: accum=r[P3] step(r[P2@P5])       */
#define OP_AggFinal      149 /* synopsis: accum=r[P1] N=P2                 */
#define OP_Expire        150
#define OP_TableLock     151 /* synopsis: iDb=P1 root=P2 write=P3          */
#define OP_Pagecount     152
#define OP_MaxPgcnt      153
#define OP_CursorHint    154
#define OP_IncMaxid      155
#define OP_Noop          156
#define OP_Explain       157

/* Properties such as "out2" or "jump" that are specified in
** comments following the "case" for each opcode in the vdbe.c
** are encoded into bitvectors as follows:
*/
#define OPFLG_JUMP        0x01  /* jump:  P2 holds jmp target */
#define OPFLG_IN1         0x02  /* in1:   P1 is an input */
#define OPFLG_IN2         0x04  /* in2:   P2 is an input */
#define OPFLG_IN3         0x08  /* in3:   P3 is an input */
#define OPFLG_OUT2        0x10  /* out2:  P2 is an output */
#define OPFLG_OUT3        0x20  /* out3:  P3 is an output */
#define OPFLG_INITIALIZER {\
/*   0 */ 0x00, 0x00, 0x00, 0x01, 0x01, 0x26, 0x26, 0x12,\
/*   8 */ 0x01, 0x01, 0x01, 0x00, 0x10, 0x03, 0x03, 0x0b,\
/*  16 */ 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x01, 0x26, 0x26,\
/*  24 */ 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26,\
/*  32 */ 0x01, 0x12, 0x01, 0x01, 0x03, 0x03, 0x01, 0x01,\
/*  40 */ 0x03, 0x03, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,\
/*  48 */ 0x09, 0x09, 0x09, 0x01, 0x01, 0x01, 0x01, 0x01,\
/*  56 */ 0x01, 0x01, 0x01, 0x23, 0x0b, 0x01, 0x01, 0x03,\
/*  64 */ 0x03, 0x03, 0x01, 0x02, 0x02, 0x08, 0x00, 0x10,\
/*  72 */ 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10,\
/*  80 */ 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,\
/*  88 */ 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,\
/*  96 */ 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,\
/* 104 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x20,\
/* 112 */ 0x10, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,\
/* 120 */ 0x00, 0x00, 0x10, 0x00, 0x04, 0x04, 0x00, 0x00,\
/* 128 */ 0x10, 0x10, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00,\
/* 136 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x10,\
/* 144 */ 0x00, 0x04, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00,\
/* 152 */ 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,}

/* The sqlite3P2Values() routine is able to run faster if it knows
** the value of the largest JUMP opcode.  The smaller the maximum
** JUMP opcode the better, so the mkopcodeh.tcl script that
** generated this include file strives to group all JUMP opcodes
** together near the beginning of the list.
*/
#define SQLITE_MX_JUMP_OPCODE  66  /* Maximum JUMP opcode */
