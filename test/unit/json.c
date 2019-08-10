#include "json/json.h"
#include "unit.h"
#include "trivia/util.h"
#include <string.h>
#include <stdbool.h>

#define INDEX_BASE 1

#define reset_to_new_path(value) \
	path = value; \
	len = strlen(value); \
	json_lexer_create(&lexer, path, len, INDEX_BASE);

#define is_next_index(value_len, value) \
	path = lexer.src + lexer.offset; \
	is(json_lexer_next_token(&lexer, &token), 0, "parse <%." #value_len "s>", \
	   path); \
	is(token.type, JSON_TOKEN_NUM, "<%." #value_len "s> is num", path); \
	is(token.num, value, "<%." #value_len "s> is " #value, path);

#define is_next_key(value) \
	len = strlen(value); \
	is(json_lexer_next_token(&lexer, &token), 0, "parse <" value ">"); \
	is(token.type, JSON_TOKEN_STR, "<" value "> is str"); \
	is(token.len, len, "len is %d", len); \
	is(strncmp(token.str, value, len), 0, "str is " value);

void
test_basic()
{
	header();
	plan(71);
	const char *path;
	int len;
	struct json_lexer lexer;
	struct json_token token;

	reset_to_new_path("[1].field1.field2['field3'][5]");
	is_next_index(3, 0);
	is_next_key("field1");
	is_next_key("field2");
	is_next_key("field3");
	is_next_index(3, 4);

	reset_to_new_path("[3].field[2].field")
	is_next_index(3, 2);
	is_next_key("field");
	is_next_index(3, 1);
	is_next_key("field");

	reset_to_new_path("[\"f1\"][\"f2'3'\"]");
	is_next_key("f1");
	is_next_key("f2'3'");

	/* Support both '.field1...' and 'field1...'. */
	reset_to_new_path(".field1");
	is_next_key("field1");
	reset_to_new_path("field1");
	is_next_key("field1");

	/* Long number. */
	reset_to_new_path("[1234]");
	is_next_index(6, 1233);

	/* Empty path. */
	reset_to_new_path("");
	is(json_lexer_next_token(&lexer, &token), 0, "parse empty path");
	is(token.type, JSON_TOKEN_END, "is str");

	/* Path with no '.' at the beginning. */
	reset_to_new_path("field1.field2");
	is_next_key("field1");

	/* Unicode. */
	reset_to_new_path("[2][6]['привет中国world']['中国a']");
	is_next_index(3, 1);
	is_next_index(3, 5);
	is_next_key("привет中国world");
	is_next_key("中国a");

	check_plan();
	footer();
}

#define check_new_path_on_error(value, errpos) \
	reset_to_new_path(value); \
	struct json_token token; \
	is(json_lexer_next_token(&lexer, &token), errpos, "error on position %d" \
	   " for <%s>", errpos, path);

struct path_and_errpos {
	const char *path;
	int errpos;
};

void
test_errors()
{
	header();
	plan(22);
	const char *path;
	int len;
	struct json_lexer lexer;
	const struct path_and_errpos errors[] = {
		/* Double [[. */
		{"[[", 2},
		/* Not string inside []. */
		{"[field]", 2},
		/* String outside of []. */
		{"'field1'.field2", 1},
		/* Empty brackets. */
		{"[]", 2},
		/* Empty string. */
		{"''", 1},
		/* Spaces between identifiers. */
		{" field1", 1},
		/* Start from digit. */
		{"1field", 1},
		{".1field", 2},
		/* Unfinished identifiers. */
		{"['field", 8},
		{"['field'", 9},
		{"[123", 5},
		{"['']", 3},
		/*
		 * Not trivial error: can not write
		 * '[]' after '.'.
		 */
		{".[123]", 2},
		/* Misc. */
		{"[.]", 2},
		/* Invalid UNICODE */
		{"['aaa\xc2\xc2']", 6},
		{".\xc2\xc2", 2},
	};
	for (size_t i = 0; i < lengthof(errors); ++i) {
		reset_to_new_path(errors[i].path);
		int errpos = errors[i].errpos;
		struct json_token token;
		is(json_lexer_next_token(&lexer, &token), errpos,
		   "error on position %d for <%s>", errpos, path);
	}

	reset_to_new_path("f.[2]")
	struct json_token token;
	json_lexer_next_token(&lexer, &token);
	is(json_lexer_next_token(&lexer, &token), 3, "can not write <field.[index]>")

	reset_to_new_path("[1]key")
	json_lexer_next_token(&lexer, &token);
	is(json_lexer_next_token(&lexer, &token), 4, "can not omit '.' before "\
	   "not a first key out of []");

	reset_to_new_path("f.")
	json_lexer_next_token(&lexer, &token);
	is(json_lexer_next_token(&lexer, &token), 3, "error in leading <.>");

	reset_to_new_path("fiel d1")
	json_lexer_next_token(&lexer, &token);
	is(json_lexer_next_token(&lexer, &token), 5, "space inside identifier");

	reset_to_new_path("field\t1")
	json_lexer_next_token(&lexer, &token);
	is(json_lexer_next_token(&lexer, &token), 6, "tab inside identifier");

	reset_to_new_path("[0]");
	is(json_lexer_next_token(&lexer, &token), 2,
	   "invalid token for index_base %d", INDEX_BASE);

	check_plan();
	footer();
}

struct test_struct {
	int value;
	struct json_token node;
};

struct test_struct *
test_struct_alloc(struct test_struct *records_pool, int *pool_idx)
{
	struct test_struct *ret = &records_pool[*pool_idx];
	*pool_idx = *pool_idx + 1;
	memset(&ret->node, 0, sizeof(ret->node));
	return ret;
}

struct test_struct *
test_add_path(struct json_tree *tree, const char *path, uint32_t path_len,
	      struct test_struct *records_pool, int *pool_idx)
{
	int rc;
	struct json_lexer lexer;
	struct json_token *parent = &tree->root;
	json_lexer_create(&lexer, path, path_len, INDEX_BASE);
	struct test_struct *field = test_struct_alloc(records_pool, pool_idx);
	while ((rc = json_lexer_next_token(&lexer, &field->node)) == 0 &&
		field->node.type != JSON_TOKEN_END) {
		struct json_token *next =
			json_tree_lookup(tree, parent, &field->node);
		if (next == NULL) {
			rc = json_tree_add(tree, parent, &field->node);
			fail_if(rc != 0);
			next = &field->node;
			field = test_struct_alloc(records_pool, pool_idx);
		}
		parent = next;
	}
	fail_if(rc != 0 || field->node.type != JSON_TOKEN_END);
	/* Release field. */
	*pool_idx = *pool_idx - 1;
	return json_tree_entry(parent, struct test_struct, node);
}

void
test_tree()
{
	header();
	plan(65);

	struct json_tree tree;
	int rc = json_tree_create(&tree);
	fail_if(rc != 0);

	struct test_struct records[7];
	for (int i = 0; i < 6; i++)
		records[i].value = i;

	const char *path1 = "[1][10]";
	const char *path2 = "[1][20].file";
	const char *path3 = "[1][20].file[2]";
	const char *path4 = "[1][20].file[8]";
	const char *path4_copy = "[1][20][\"file\"][8]";
	const char *path_unregistered = "[1][3]";

	int records_idx = 0;
	struct test_struct *node, *node_tmp;
	node = test_add_path(&tree, path1, strlen(path1), records,
			     &records_idx);
	is(node, &records[1], "add path '%s'", path1);

	node = test_add_path(&tree, path2, strlen(path2), records,
			     &records_idx);
	is(node, &records[3], "add path '%s'", path2);

	node = test_add_path(&tree, path3, strlen(path3), records,
			     &records_idx);
	is(node, &records[4], "add path '%s'", path3);

	node = test_add_path(&tree, path4, strlen(path4), records,
			     &records_idx);
	is(node, &records[5], "add path '%s'", path4);

	node = test_add_path(&tree, path4_copy, strlen(path4_copy), records,
			     &records_idx);
	is(node, &records[5], "add path '%s'", path4_copy);

	node = json_tree_lookup_path_entry(&tree, &tree.root, path1,
					   strlen(path1), INDEX_BASE,
					   struct test_struct, node);
	is(node, &records[1], "lookup path '%s'", path1);

	node = json_tree_lookup_path_entry(&tree, &tree.root, path2,
					   strlen(path2), INDEX_BASE,
					   struct test_struct, node);
	is(node, &records[3], "lookup path '%s'", path2);

	node = json_tree_lookup_path_entry(&tree, &tree.root, path_unregistered,
					   strlen(path_unregistered), INDEX_BASE,
					   struct test_struct, node);
	is(node, NULL, "lookup unregistered path '%s'", path_unregistered);

	/* Test iterators. */
	struct json_token *token = NULL, *tmp;
	const struct json_token *tokens_preorder[] =
		{&records[0].node, &records[1].node, &records[2].node,
		 &records[3].node, &records[4].node, &records[5].node};
	int cnt = sizeof(tokens_preorder)/sizeof(tokens_preorder[0]);
	int idx = 0;

	json_tree_foreach_preorder(token, &tree.root) {
		if (idx >= cnt)
			break;
		struct test_struct *t1 =
			json_tree_entry(token, struct test_struct, node);
		struct test_struct *t2 =
			json_tree_entry(tokens_preorder[idx],
					struct test_struct, node);
		is(token, tokens_preorder[idx],
		   "test foreach pre order %d: have %d expected of %d",
		   idx, t1->value, t2->value);
		++idx;
	}
	is(idx, cnt, "records iterated count %d of %d", idx, cnt);

	const struct json_token *tree_nodes_postorder[] =
		{&records[1].node, &records[4].node, &records[5].node,
		 &records[3].node, &records[2].node, &records[0].node};
	cnt = sizeof(tree_nodes_postorder)/sizeof(tree_nodes_postorder[0]);
	idx = 0;
	json_tree_foreach_postorder(token, &tree.root) {
		if (idx >= cnt)
			break;
		struct test_struct *t1 =
			json_tree_entry(token, struct test_struct, node);
		struct test_struct *t2 =
			json_tree_entry(tree_nodes_postorder[idx],
					struct test_struct, node);
		is(token, tree_nodes_postorder[idx],
		   "test foreach post order %d: have %d expected of %d",
		   idx, t1->value, t2->value);
		++idx;
	}
	is(idx, cnt, "records iterated count %d of %d", idx, cnt);

	idx = 0;
	json_tree_foreach_safe(token, &tree.root, tmp) {
		if (idx >= cnt)
			break;
		struct test_struct *t1 =
			json_tree_entry(token, struct test_struct, node);
		struct test_struct *t2 =
			json_tree_entry(tree_nodes_postorder[idx],
					struct test_struct, node);
		is(token, tree_nodes_postorder[idx],
		   "test foreach safe order %d: have %d expected of %d",
		   idx, t1->value, t2->value);
		++idx;
	}
	is(idx, cnt, "records iterated count %d of %d", idx, cnt);

	idx = 0;
	json_tree_foreach_entry_preorder(node, &tree.root, struct test_struct,
					 node) {
		if (idx >= cnt)
			break;
		struct test_struct *t =
			json_tree_entry(tokens_preorder[idx],
					struct test_struct, node);
		is(&node->node, tokens_preorder[idx],
		   "test foreach entry pre order %d: have %d expected of %d",
		   idx, node->value, t->value);
		idx++;
	}
	is(idx, cnt, "records iterated count %d of %d", idx, cnt);

	idx = 0;
	json_tree_foreach_entry_postorder(node, &tree.root, struct test_struct,
					  node) {
		if (idx >= cnt)
			break;
		struct test_struct *t =
			json_tree_entry(tree_nodes_postorder[idx],
					struct test_struct, node);
		is(&node->node, tree_nodes_postorder[idx],
		   "test foreach entry post order %d: have %d expected of %d",
		   idx, node->value, t->value);
		idx++;
	}
	is(idx, cnt, "records iterated count %d of %d", idx, cnt);

	/* Test record deletion. */
	is(records[3].node.max_child_idx, 7, "max_child_index %d expected of %d",
	   records[3].node.max_child_idx, 7);
	json_tree_del(&tree, &records[5].node);
	is(records[3].node.max_child_idx, 1, "max_child_index %d expected of %d",
	   records[3].node.max_child_idx, 1);
	json_tree_del(&tree, &records[4].node);
	is(records[3].node.max_child_idx, -1, "max_child_index %d expected of %d",
	   records[3].node.max_child_idx, -1);
	node = json_tree_lookup_path_entry(&tree, &tree.root, path3,
					   strlen(path3), INDEX_BASE,
					   struct test_struct, node);
	is(node, NULL, "lookup removed path '%s'", path3);

	node = json_tree_lookup_path_entry(&tree, &tree.root, path4,
					   strlen(path4), INDEX_BASE,
					   struct test_struct, node);
	is(node, NULL, "lookup removed path '%s'", path4);

	node = json_tree_lookup_path_entry(&tree, &tree.root, path2,
					   strlen(path2), INDEX_BASE,
					   struct test_struct, node);
	is(node, &records[3], "lookup path was not corrupted '%s'", path2);

	const struct json_token *tree_nodes_postorder_new[] =
		{&records[1].node, &records[3].node,
		 &records[2].node, &records[0].node};
	cnt = sizeof(tree_nodes_postorder_new) /
	      sizeof(tree_nodes_postorder_new[0]);
	idx = 0;
	json_tree_foreach_entry_safe(node, &tree.root, struct test_struct,
				     node, node_tmp) {
		if (idx >= cnt)
			break;
		struct test_struct *t =
			json_tree_entry(tree_nodes_postorder_new[idx],
					struct test_struct, node);
		is(&node->node, tree_nodes_postorder_new[idx],
		   "test foreach entry safe order %d: have %d expected of %d",
		   idx, node->value, t->value);
		json_tree_del(&tree, &node->node);
		idx++;
	}
	is(idx, cnt, "records iterated count %d of %d", idx, cnt);

	records_idx = 0;
	node = test_add_path(&tree, path2, strlen(path2), records, &records_idx);
	fail_if(&node->node != &records[2].node);
	is(json_token_is_leaf(&records[1].node), false, "interm node is not leaf");
	is(json_token_is_leaf(&records[2].node), true, "last node is leaf");

	node = test_add_path(&tree, path3, strlen(path3), records, &records_idx);
	fail_if(&node->node != &records[3].node);
	is(json_token_is_leaf(&records[2].node), false,
	   "last node became interm - it can't be leaf anymore");
	is(json_token_is_leaf(&records[3].node), true, "last node is leaf");

	json_tree_foreach_entry_safe(node, &tree.root, struct test_struct,
				     node, node_tmp)
		json_tree_del(&tree, &node->node);

	/* Test multikey tokens. */
	records_idx = 0;
	node = test_add_path(&tree, path1, strlen(path1), records, &records_idx);
	is(node, &records[1], "add path '%s'", path1);
	token->type = JSON_TOKEN_ANY;
	node = json_tree_lookup_entry(&tree, &records[0].node, token,
				      struct test_struct, node);
	is(node->node.num, 9, "lookup any token in non-multikey node");

	/* Can't attach ANY token to non-leaf node. Cleanup. */
	json_tree_del(&tree, &records[1].node);
	records_idx--;

	const char *path_multikey = "[1][*][\"data\"]";
	node = test_add_path(&tree, path_multikey, strlen(path_multikey),
			     records, &records_idx);
	is(node, &records[2], "add path '%s'", path_multikey);

	node = json_tree_lookup_path_entry(&tree, &tree.root, path_multikey,
					   strlen(path_multikey), INDEX_BASE,
					   struct test_struct, node);
	is(node, &records[2], "lookup path '%s'", path_multikey);

	token = &records[records_idx++].node;
	token->type = JSON_TOKEN_NUM;
	token->num = 3;
	node = json_tree_lookup_entry(&tree, &records[0].node, token,
				      struct test_struct, node);
	is(node, &records[1], "lookup numeric token in multikey node");

	token->type = JSON_TOKEN_ANY;
	node = json_tree_lookup_entry(&tree, &records[0].node, token,
				      struct test_struct, node);
	is(node, &records[1], "lookup any token in multikey node");

	token->type = JSON_TOKEN_STR;
	token->str = "str";
	token->len = strlen("str");
	node = json_tree_lookup_entry(&tree, &records[0].node, token,
				      struct test_struct, node);
	is(node, &records[1], "lookup string token in multikey node");

	json_tree_foreach_entry_safe(node, &tree.root, struct test_struct,
				     node, node_tmp)
		json_tree_del(&tree, &node->node);
	json_tree_destroy(&tree);

	check_plan();
	footer();
}

void
test_path_cmp()
{
	const char *a = "Data[1][\"FIO\"].fname";
	uint32_t a_len = strlen(a);
	const struct path_and_errpos rc[] = {
		{a, 0},
		{"[\"Data\"][1].FIO[\"fname\"]", 0},
		{"Data[1]", 1},
		{"Data[1][\"FIO\"].fname[1]", -1},
		{"Data[1][\"Info\"].fname[1]", -1},
	};
	header();
	plan(lengthof(rc) + 3);
	for (size_t i = 0; i < lengthof(rc); ++i) {
		const char *path = rc[i].path;
		int errpos = rc[i].errpos;
		int rc = json_path_cmp(a, a_len, path, strlen(path),
				       INDEX_BASE);
		if (rc > 0) rc = 1;
		if (rc < 0) rc = -1;
		is(rc, errpos, "path cmp result \"%s\" with \"%s\": "
		   "have %d, expected %d", a, path, rc, errpos);
	}
	char *multikey_a = "Data[*][\"FIO\"].fname[*]";
	char *multikey_b = "[\"Data\"][*].FIO[\"fname\"][*]";
	int ret = json_path_cmp(multikey_a, strlen(multikey_a), multikey_b,
				strlen(multikey_b), INDEX_BASE);
	is(ret, 0, "path cmp result \"%s\" with \"%s\": have %d, expected %d",
	   multikey_a, multikey_b, ret, 0);

	const char *invalid = "Data[[1][\"FIO\"].fname";
	ret = json_path_validate(a, strlen(a), INDEX_BASE);
	is(ret, 0, "path %s is valid", a);
	ret = json_path_validate(invalid, strlen(invalid), INDEX_BASE);
	is(ret, 6, "path %s error pos %d expected %d", invalid, ret, 6);

	check_plan();
	footer();
}

void
test_path_snprint()
{
	header();
	plan(9);

	struct json_tree tree;
	int rc = json_tree_create(&tree);
	fail_if(rc != 0);
	struct test_struct records[6];
	const char *path = "[1][*][20][\"file\"][8]";
	int path_len = strlen(path);

	int records_idx = 0;
	struct test_struct *node, *node_tmp;
	node = test_add_path(&tree, path, path_len, records, &records_idx);
	fail_if(&node->node != &records[4].node);

	char buf[64];
	int bufsz = sizeof(buf);

	rc = json_tree_snprint_path(buf, bufsz, &node->node, INDEX_BASE);
	is(rc, path_len, "full path - retval");
	is(buf[path_len], '\0', "full path - terminating nul");
	is(memcmp(buf, path, path_len), 0, "full path - output");

	bufsz = path_len - 5;
	rc = json_tree_snprint_path(buf, bufsz, &node->node, INDEX_BASE);
	is(rc, path_len, "truncated path - retval");
	is(buf[bufsz - 1], '\0', "truncated path - terminating nul");
	is(memcmp(buf, path, bufsz - 1), 0, "truncated path - output");

	rc = json_tree_snprint_path(buf, 1, &node->node, INDEX_BASE);
	is(rc, path_len, "1-byte buffer - retval");
	is(buf[0], '\0', "1-byte buffer - terminating nul");

	rc = json_tree_snprint_path(NULL, 0, &node->node, INDEX_BASE);
	is(rc, path_len, "0-byte buffer - retval");

	json_tree_foreach_entry_safe(node, &tree.root, struct test_struct,
				     node, node_tmp)
		json_tree_del(&tree, &node->node);
	json_tree_destroy(&tree);

	check_plan();
	footer();
}

void
test_path_multikey()
{
	static struct {
		const char *str;
		int rc;
	} test_cases[] = {
		{"", 0},
		{"[1].Data[1].extra[1]", 20},
		{"[*].Data[1].extra[1]", 0},
		{"[*].Data[*].extra[1]", 0},
		{"[1].Data[*].extra[1]", 8},
		{"[1].Data[1].extra[*]", 17},
	};

	header();
	plan(lengthof(test_cases));
	for (unsigned i = 0; i < lengthof(test_cases); i++) {
		int rc = json_path_multikey_offset(test_cases[i].str,
						   strlen(test_cases[i].str),
						   INDEX_BASE);
		is(rc, test_cases[i].rc, "Test json_path_multikey_offset with "
		   "%s: have %d expected %d", test_cases[i].str, rc,
		   test_cases[i].rc);
	}
	check_plan();
	footer();
}

int
main()
{
	header();
	plan(6);

	test_basic();
	test_errors();
	test_tree();
	test_path_cmp();
	test_path_snprint();
	test_path_multikey();

	int rc = check_plan();
	footer();
	return rc;
}
