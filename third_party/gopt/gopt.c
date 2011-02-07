/* gopt.c version 8.1: tom.viza@gmail.com PUBLIC DOMAIN 2003-8 */
/*
I, Tom Vajzovic, am the author of this software and its documentation and
permanently abandon all copyright and other intellectual property rights in
them, including the right to be identified as the author.

I am fairly certain that this software does what the documentation says it
does, but I cannot guarantee that it does, or that it does what you think it
should, and I cannot guarantee that it will not have undesirable side effects.

You are free to use, modify and distribute this software as you please, but
you do so at your own risk.  If you remove or hide this warning then you are
responsible for any problems encountered by people that you make the software
available to.

Before modifying or distributing this software I ask that you would please
read http://www.purposeful.co.uk/tfl/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gopt.h"

#ifdef USE_SYSEXITS
#include <sysexits.h>
#else
#define EX_OSERR EXIT_FAILURE
#define EX_USAGE EXIT_FAILURE
#endif

struct opt_spec_s {
  int key;
  int flags;
  const char *shorts;
  const char* const *longs;
  const char *help_arg;
  const char *help;
};
typedef struct opt_spec_s opt_spec_t;

struct opt_s {
  int key;
  const char *arg;
};
typedef struct opt_s opt_t;

void *gopt_sort( int *argc, const char **argv, const void *opt_specs ){
  void *opts;
  {{{
    const char* const *arg_p= argv + 1;
    size_t opt_count= 1;
    for( ; *arg_p; ++arg_p )
      if( '-' == (*arg_p)[0] && (*arg_p)[1] )
      {
        if( '-' == (*arg_p)[1] )
          if( (*arg_p)[2] )
            ++opt_count;
          else
            break;
        else {
          const opt_spec_t *opt_spec_p= opt_specs;
          for( ; opt_spec_p-> key; ++opt_spec_p )
            if( strchr( opt_spec_p-> shorts, (*arg_p)[1] )){
              opt_count+= opt_spec_p-> flags & GOPT_ARG ? 1 : strlen( (*arg_p) + 1 );
              break;
            }
        }
      }
    opts= malloc( opt_count * sizeof(opt_t) );
  }}}
  {
    const char **arg_p= argv + 1;
    const char **next_operand= arg_p;
    opt_t *next_option= opts;

    if( ! opts ){
      perror( argv[0] );
      exit( EX_OSERR );
    }
    for( ; *arg_p; ++arg_p )
      if( '-' == (*arg_p)[0] && (*arg_p)[1] )
        if( '-' == (*arg_p)[1] )
          if( (*arg_p)[2] )
          {{{
            const opt_spec_t *opt_spec_p= opt_specs;
            const char* const *longs= opt_spec_p-> longs;
            next_option-> key= 0;
            while( *longs ){
              const char *option_cp= (*arg_p) + 2;
              const char *name_cp= *longs;
              while( *option_cp && *option_cp == *name_cp ){
                ++option_cp;
                ++name_cp;
              }
              if( '=' == *option_cp || !*option_cp ){
                if( *name_cp ){
                  if( next_option-> key ){
                    fprintf( stderr, "%s: --%.*s: abbreviated option is ambiguous\n", argv[0], (int)( option_cp -( (*arg_p) + 2 )), (*arg_p) + 2 );
                    free( opts );
                    exit( EX_USAGE );
                  }
                  next_option-> key= opt_spec_p-> key;
                }
                else {
                  next_option-> key= opt_spec_p-> key;
                  goto found_long;
                }
              }
              if( !*++longs ){
                ++opt_spec_p;
                if( opt_spec_p-> key )
                  longs= opt_spec_p-> longs;
              }
            }
            if( ! next_option-> key ){
              fprintf( stderr, "%s: --%.*s: unknown option\n", argv[0], (int)strcspn( (*arg_p) + 2, "=" ), (*arg_p) + 2 );
              free( opts );
              exit( EX_USAGE );
            }
            for( opt_spec_p= opt_specs; opt_spec_p-> key != next_option-> key; ++opt_spec_p );
            found_long:

            if( !( opt_spec_p-> flags & GOPT_REPEAT )){
              const opt_t *opt_p= opts;
              for( ; opt_p != next_option; ++opt_p )
                if( opt_p-> key == opt_spec_p-> key ){
                  fprintf( stderr, "%s: --%.*s: option may not be repeated (in any long or short form)\n", argv[0], (int)strcspn( (*arg_p) + 2, "=" ), (*arg_p) + 2 );
                  free( opts );
                  exit( EX_USAGE );
                }
            }
            if( opt_spec_p-> flags & GOPT_ARG ){
              next_option-> arg= strchr( (*arg_p) + 2, '=' ) + 1;
              if( (char*)0 + 1 == next_option-> arg ){
                ++arg_p;
                if( !*arg_p || ('-' == (*arg_p)[0] && (*arg_p)[1] )){
                  fprintf( stderr, "%s: --%s: option requires an option argument\n", argv[0], (*(arg_p-1)) + 2 );
                  free( opts );
                  exit( EX_USAGE );
                }
                next_option-> arg= *arg_p;
              }
            }
            else {
              if( strchr( (*arg_p) + 2, '=' )){
                fprintf( stderr, "%s: --%.*s: option may not take an option argument\n", argv[0], (int)strcspn( (*arg_p) + 2, "=" ), (*arg_p) + 2 );
                free( opts );
                exit( EX_USAGE );
              }
              next_option-> arg= NULL;
            }
            ++next_option;
          }}}
          else {
            for( ++arg_p; *arg_p; ++arg_p )
              *next_operand++= *arg_p;
            break;
          }
        else
        {{{
          const char *short_opt= (*arg_p) + 1;
          for( ;*short_opt; ++short_opt ){
            const opt_spec_t *opt_spec_p= opt_specs;

            for( ; opt_spec_p-> key; ++opt_spec_p )
              if( strchr( opt_spec_p-> shorts, *short_opt )){
                if( !( opt_spec_p-> flags & GOPT_REPEAT )){
                  const opt_t *opt_p= opts;
                  for( ; opt_p != next_option; ++opt_p )
                    if( opt_p-> key == opt_spec_p-> key ){
                      fprintf( stderr, "%s: -%c: option may not be repeated (in any long or short form)\n", argv[0], *short_opt );
                      free( opts );
                      exit( EX_USAGE );
                    }
                }
                next_option-> key= opt_spec_p-> key;

                if( opt_spec_p-> flags & GOPT_ARG ){
                  if( short_opt[1] )
                    next_option-> arg= short_opt + 1;

                  else {
                    ++arg_p;
                    if( !*arg_p || ('-' == (*arg_p)[0] && (*arg_p)[1])){
                      fprintf( stderr, "%s: -%c: option requires an option argument\n", argv[0], *short_opt );
                      free( opts );
                      exit( EX_USAGE );
                    }
                    next_option-> arg= *arg_p;
                  }
                  ++next_option;
                  goto break_2;
                }
                next_option-> arg= NULL;
                ++next_option;
                goto continue_2;
              }
            fprintf( stderr, "%s: -%c: unknown option\n", argv[0], *short_opt );
            free( opts );
            exit( EX_USAGE );
            continue_2: ;
          }
          break_2: ;
        }}}
      else
        *next_operand++= *arg_p;

    next_option-> key= 0;
    *next_operand= NULL;
    *argc= next_operand - argv;
  }
  return opts;
}

size_t gopt( const void *vptr_opts, int key ){
  const opt_t *opts= vptr_opts;
  size_t count= 0;
  for( ; opts-> key; ++opts )
    count+= opts-> key == key;

  return count;
}

size_t gopt_arg( const void *vptr_opts, int key, const char **arg ){
  const opt_t *opts= vptr_opts;
  size_t count= 0;

  for( ; opts-> key; ++opts )
    if( opts-> key == key ){
      if( ! count )
        *arg= opts-> arg;
      ++count;
    }
  return count;
}

const char *gopt_arg_i( const void *vptr_opts, int key, size_t i ){
  const opt_t *opts= vptr_opts;

  for( ; opts-> key; ++opts )
    if( opts-> key == key ){
      if( ! i )
        return opts-> arg;
      --i;
    }
  return NULL;
}

size_t gopt_args( const void *vptr_opts, int key, const char **args, size_t args_len ){
  const char **args_stop= args + args_len;
  const char **args_ptr= args;
  const opt_t *opts= vptr_opts;

  for( ; opts-> key; ++opts )
    if( opts-> key == key ){
      if( args_stop == args_ptr )
        return args_len + gopt( opts, key );

      *args_ptr++= opts-> arg;
    }
  if( args_stop != args_ptr )
    *args_ptr= NULL;
  return args_ptr - args;
}

void gopt_free( void *vptr_opts ){
  free( vptr_opts );
}

void gopt_help(const void *opt_def){
  const struct opt_spec_s *opt = opt_def;

  /*
   * layout:
   * padding: 2 spaces ("  ")
   * short option: 4 chars, padded with spaces ("    " or "-x  " or "-x, ")
   * long option: 20 chars, padded with spaces ("--option                ")
   * help: rest of line: 54
   * help padding: 25 spaces
   */

  const int long_opt_width = 18; /* not counting leading "--" */
  const int help_width = 54;
  const char help_padding[] = "                         ";

    while (opt->key) {
    const char *shorts = opt->shorts;
    char has_shorts = 0;
    printf("  ");
    if (*shorts) {
      has_shorts = 1;
      printf("-%c", *shorts++);
    } else
      printf("  ");

    const char *const *longs = opt->longs;
    if (*longs) {
      if (has_shorts)
	printf(", ");
      else
	printf("  ");
      if (opt->help_arg)
	printf("--%s%-*s", *longs, long_opt_width - (int) strlen(*longs),
               opt->help_arg);
      else
	printf("--%-*s", long_opt_width, *longs);
    }
    if (opt->help) {
      const char *help = opt->help;
      while (strlen(help) > help_width) {
	const char *p = help + help_width;
	while (p > help && *p != ' ')
	  p--;
	if (p == help)
	  p = help + help_width;
	printf("%.*s\n", (int) (p - help), help);
	help = p;
	if (strlen(help))
	  printf("%s", help_padding);
      }
      puts(help);
    } else
      puts("");
    opt++;
  }
}
