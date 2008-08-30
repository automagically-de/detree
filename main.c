#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

typedef struct {
	FILE *f;
	guint32 max_line_length;
	gchar *line_buffer;
	GSList *line_defs;
	guint32 n_columns;
	GtkTreeStore *tree_store;
	GtkTreeIter *last_iter;
	guint32 n_max_cached_iters;
	GtkTreeIter **iter_cache;
} Global;

typedef gboolean (* LineCallback)(Global *global);

typedef struct {
	const gchar *type;
	guint32 n_cols;
	LineCallback callback;
} LineType;

static gboolean line_path_cb(Global *global);
static gboolean line_indent_cb(Global *global);
static gboolean line_value_cb(Global *global);

static LineType known_line_types[] = {
	{ "path",          1, line_path_cb },
	{ "indent",        1, line_indent_cb },
	{ "value",         2, line_value_cb },
	{ NULL, 0, NULL }
};

enum {
	COL_TREE,
	COL_VALUE,
	N_COLUMNS
};

static Global *global_init(int argc, char **argv);
static void global_cleanup(Global *global);
static gboolean gui_init(Global *global);
static gboolean data_init(Global *global);

int main(int argc, char *argv[])
{
	Global *global;

	gtk_init(&argc, &argv);
	global = global_init(argc, argv);
	if((global->f == NULL) || (global->line_defs == NULL)) {
		global_cleanup(global);
		return EXIT_FAILURE;
	}

	global->tree_store = gtk_tree_store_new(global->n_columns,
		G_TYPE_STRING,
		G_TYPE_STRING);

	data_init(global);

	gui_init(global);

	gtk_main();

	return EXIT_SUCCESS;
}

static gboolean gui_init(Global *global)
{
	GtkWidget *window, *tv, *sw;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "inspect tree");
	gtk_widget_set_size_request(window, 400, 500);
	g_signal_connect(G_OBJECT(window), "delete-event",
		G_CALLBACK(gtk_main_quit), NULL);

	sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(window), sw);

	tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(global->tree_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), TRUE);
	gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(tv), TRUE);
	gtk_container_add(GTK_CONTAINER(sw), tv);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("tree item", renderer,
		"text", COL_TREE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tv), column);

	if(global->n_columns > 1) {
		renderer = gtk_cell_renderer_text_new();
		column = gtk_tree_view_column_new_with_attributes("value", renderer,
			"text", COL_VALUE, NULL);
		gtk_tree_view_append_column(GTK_TREE_VIEW(tv), column);
	}

	gtk_widget_show_all(window);

	return TRUE;
}

static gboolean data_init(Global *global)
{
	GSList *item;
	LineType *lt;

	while(!feof(global->f)) {
		for(item = global->line_defs; item != NULL; item = item->next) {
			lt = item->data;
			if(!fgets(global->line_buffer,
				global->max_line_length, global->f))
				return FALSE;
			g_strchomp(global->line_buffer);
			lt->callback(global);
		}
	}
	return TRUE;
}

static LineType *find_line_type(const gchar *tname)
{
	gint32 i;

	for(i = 0; known_line_types[i].type != NULL; i ++)
		if(strcmp(known_line_types[i].type, tname) == 0)
			return &(known_line_types[i]);
	return NULL;
}

static Global *global_init(int argc, char **argv)
{
	Global *global;
	LineType *lt;
	gint i;

	global = g_new0(Global, 1);
	global->max_line_length = 2048;
	global->n_max_cached_iters = 42;

	for(i = 1; i < argc; i ++) {
		if((strcmp(argv[i], "-l") == 0) && (++ i < argc)) {
			lt = find_line_type(argv[i]);
			if(lt == NULL) {
				g_warning("%s: unknown line type '%s'", argv[0], argv[i]);
				continue;
			}
			global->line_defs = g_slist_append(global->line_defs, lt);
			global->n_columns = MIN(N_COLUMNS,
				MAX(global->n_columns, lt->n_cols));
		} else {
			g_warning("%s: unknown option '%s'\n", argv[0], argv[i]);
		}
	}

	if(global->line_defs == NULL) {
		g_warning("%s: no line definitions given", argv[0]);
	}

	global->line_buffer = g_new0(gchar, global->max_line_length);
	global->iter_cache = g_new0(GtkTreeIter *, global->n_max_cached_iters);
	if(global->f == NULL)
		global->f = stdin;

	return global;
}

static void global_cleanup(Global *global)
{
	if((global->f != NULL) && (global->f != stdin))
		fclose(global->f);
	g_free(global->line_buffer);
	g_free(global->iter_cache);
	g_free(global);
}

static GtkTreeIter *get_node(Global *global, const gchar *text,
	GtkTreeIter *parent)
{
	GtkTreeIter *iter;
	GValue *value;

	iter = g_new0(GtkTreeIter, 1);
	if(!gtk_tree_model_iter_children(GTK_TREE_MODEL(global->tree_store),
		iter, parent)) {
		g_free(iter);
		return NULL;
	}
	do {
		value = g_new0(GValue, 1);
		gtk_tree_model_get_value(GTK_TREE_MODEL(global->tree_store), iter,
			COL_TREE, value);
		if(strcmp(g_value_get_string(value), text) == 0) {
			g_value_unset(value);
			g_free(value);
			return iter;
		}
		g_value_unset(value);
		g_free(value);
	} while(gtk_tree_model_iter_next(
		GTK_TREE_MODEL(global->tree_store), iter));

	g_free(iter);
	return NULL;
}

static GtkTreeIter *create_node(Global *global, const gchar *text,
	GtkTreeIter *parent)
{
	GtkTreeIter *iter;

	iter = g_new0(GtkTreeIter, 1);
	gtk_tree_store_append(global->tree_store, iter, parent);
	gtk_tree_store_set(global->tree_store, iter,
		COL_TREE, g_strdup(text),
		-1);

	return iter;
}

static gboolean line_path_cb(Global *global)
{
	GtkTreeIter *iter, *parentiter = NULL;
	gchar **path, **pptr;

	path = g_strsplit(global->line_buffer, "/", 0);
	for(pptr = path; *pptr != NULL; pptr ++) {
		if((strlen(*pptr) == 0) || (strcmp(*pptr, ".") == 0))
			continue;
		iter = get_node(global, *pptr, parentiter);
		if(iter == NULL)
			iter = create_node(global, *pptr, parentiter);
		global->last_iter = iter;
		if(parentiter != NULL)
			g_free(parentiter);
		parentiter = iter;
	}

	g_strfreev(path);

	return TRUE;
}

static gboolean line_indent_cb(Global *global)
{
	guint32 level = 0;
	gchar *text;
	GtkTreeIter *iter;

	text = global->line_buffer;
	while(*text == ' ') {
		text ++;
		level ++;
	}
	g_return_val_if_fail(level < global->n_max_cached_iters, FALSE);

	iter = create_node(global, text, (level == 0) ? NULL :
		global->iter_cache[level - 1]);

	if(global->iter_cache[level])
		g_free(global->iter_cache[level]);
	global->iter_cache[level] = iter;
	while((++ level < global->n_max_cached_iters) &&
		(global->iter_cache[level] != NULL)) {
		g_free(global->iter_cache[level]);
		global->iter_cache[level] = NULL;
	}
	return TRUE;
}

static gboolean line_value_cb(Global *global)
{
	gtk_tree_store_set(global->tree_store, global->last_iter,
		COL_VALUE, g_strdup(global->line_buffer),
		-1);

	return TRUE;
}

