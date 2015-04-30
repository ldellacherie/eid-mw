#include "certs.h"
#include "verify.h"
#include "prefs.h"
#include "gtk_globals.h"

#include <string.h>

#include <gtk/gtk.h>

#include <openssl/x509.h>
#include <openssl/x509v3.h>

enum certs {
	Root,
	CA,
	CERT_RN_FILE,
	Authentication,
	Signature,
	CERTS_COUNT,
};

static GtkTreeStore* certificates = NULL;
static GtkTreeIter* iters[CERTS_COUNT];
static GdkPixbuf* good_certificate;
static GdkPixbuf* bad_certificate;
static GdkPixbuf* warn_certificate;
static GdkPixbuf* unchecked_certificate;

static GtkTreeIter* get_iter_for(char* which) {
	GtkTreeIter* parent;
	enum certs w = CERTS_COUNT;

#define char_to_enum(c) if(!strcmp(#c, which)) { w = c; }
	char_to_enum(Root);
	char_to_enum(CA);
	char_to_enum(CERT_RN_FILE);
	char_to_enum(Authentication);
	char_to_enum(Signature);
#undef char_to_enum

	if(w == CERTS_COUNT) {
		// invalid/unknown certificate
		return NULL;
	}
	if(iters[w]) {
	       	return iters[w];
	}
	switch(w) {
		case CA:
		case CERT_RN_FILE:
			parent = get_iter_for("Root");
			break;
		case Authentication:
		case Signature:
			parent = get_iter_for("CA");
			break;
		default:
			parent = NULL;
	}
	iters[w] = malloc(sizeof(GtkTreeIter));
	gtk_tree_store_append(certificates, iters[w], parent);
	return iters[w];
}

struct tree_store_data {
	char* which;
	gint* columns;
	GValue* values;
	gint n_values;
	void(*free)(struct tree_store_data*);
};

static void tst_free_simple(struct tree_store_data* dat) {
	int i;
	free(dat->which);
	free(dat->columns);
	for(i=0;i<dat->n_values; i++) {
		g_value_unset(&(dat->values[i]));
	}
	free(dat->values);
	free(dat);
}

static gboolean tst_helper(gpointer user_data) {
	struct tree_store_data* dat = (struct tree_store_data*)user_data;
	GtkTreeIter* iter = get_iter_for(dat->which);

	if(!iter) {
		return FALSE;
	}

	gtk_tree_store_set_valuesv(certificates, iter, dat->columns, dat->values, dat->n_values);
	dat->free(dat);

	return FALSE;
}

void tst_set(char* w, gint* c, GValue* v, gint n) {
	struct tree_store_data* dat = malloc(sizeof(struct tree_store_data));
	dat->which = strdup(w);
	dat->columns = c;
	dat->values = v;
	dat->n_values = n;
	dat->free = tst_free_simple;
	g_main_context_invoke(NULL, tst_helper, dat);
}

static void ensure_cert() {
	if(G_UNLIKELY(certificates == NULL)) {
		certificates = gtk_tree_store_new(CERT_COL_NCOLS,
				G_TYPE_STRING, // label (short description)
				GDK_TYPE_PIXBUF, // image
				G_TYPE_STRING, // valid from
				G_TYPE_BOOLEAN, // valid from in past?
				G_TYPE_STRING, // valid through 
				G_TYPE_BOOLEAN, // valid through in future?
				G_TYPE_STRING, // use
				G_TYPE_BOOLEAN, // validity
				G_TYPE_STRING, // description (multi-line field 
				G_TYPE_POINTER); // data (X509*)
	}
}

void add_certificate(char* label, void* data, int len) {
	X509 *cert = NULL;
	BIO *bio = BIO_new(BIO_s_mem());
	char *buf;
	size_t size;
	gint cols=8;
	gint *columns;
	GValue *vals;

	ensure_cert();
	if(!strcmp(label, "CERT_RN_FILE")) {
		add_verify_data(label, data, len);
	}
	if(d2i_X509(&cert, (const unsigned char**)&data, len) == NULL) {
		g_warning("Could not parse %s certificate", label);
		return;
	}

	columns = calloc(sizeof(gint),cols);
	vals = calloc(sizeof(GValue),cols);

	columns[0] = CERT_COL_LABEL;
	g_value_init(&(vals[0]), G_TYPE_STRING);
	char* str = describe_cert(label, cert);
	g_value_take_string(&(vals[0]), g_strdup(str));
	free(str);

	columns[1] = CERT_COL_IMAGE;
	g_value_init(&(vals[1]), GDK_TYPE_PIXBUF);
	g_value_set_instance(&(vals[1]), unchecked_certificate);

	ASN1_TIME_print(bio, X509_get_notBefore(cert));
	buf = malloc((size = BIO_ctrl_pending(bio)) + 1);
	BIO_read(bio, buf, (int)size);
	buf[size] = '\0';
	columns[2] = CERT_COL_VALIDFROM;
	g_value_init(&(vals[2]), G_TYPE_STRING);
	g_value_set_string(&(vals[2]), buf);
	
	ASN1_TIME_print(bio, X509_get_notAfter(cert));
	buf = malloc((size = BIO_ctrl_pending(bio)) + 1);
	BIO_read(bio, buf, (int)size);
	buf[size] = '\0';
	columns[3] = CERT_COL_VALIDTO;
	g_value_init(&(vals[3]), G_TYPE_STRING);
	g_value_set_string(&(vals[3]), buf);

	columns[4] = CERT_COL_DESC;
	g_value_init(&(vals[4]), G_TYPE_STRING);
	g_value_take_string(&(vals[4]), detail_cert(label, cert));

	columns[5] = CERT_COL_USE;
	g_value_init(&(vals[5]), G_TYPE_STRING);
	g_value_take_string(&(vals[5]), get_use_flags(label, cert));

	columns[6] = CERT_COL_VALIDFROM_PAST;
	g_value_init(&(vals[6]), G_TYPE_BOOLEAN);
	g_value_set_boolean(&(vals[6]), (X509_cmp_current_time(X509_get_notBefore(cert)) < 0) ? FALSE : TRUE);

	columns[7] = CERT_COL_VALIDTO_FUTURE;
	g_value_init(&(vals[7]), G_TYPE_BOOLEAN);
	g_value_set_boolean(&(vals[7]), (X509_cmp_current_time(X509_get_notAfter(cert)) > 0) ? FALSE : TRUE);

	BIO_free(bio);

	tst_set(label, columns, vals, cols);
}

GtkTreeModel* certificates_get_model() {
	ensure_cert();
	return GTK_TREE_MODEL(certificates);
}

gboolean real_clear(gpointer data G_GNUC_UNUSED) {
	int i;
	gtk_tree_store_clear(certificates);
	for(i=0; i<CERTS_COUNT; i++) {
		iters[i]=NULL;
	}
	return FALSE;
}

void certs_init() {
	GSettings* sets = get_prefs();
	g_settings_bind(sets, "showlog",
			G_OBJECT(gtk_builder_get_object(builder, "validate_always")),
			"active", 0);
	unchecked_certificate = gdk_pixbuf_from_pixdata(&certificate_large, FALSE, NULL);
	bad_certificate = gdk_pixbuf_from_pixdata(&certificate_bad, FALSE, NULL);
	warn_certificate = gdk_pixbuf_from_pixdata(&certificate_warn, FALSE, NULL);
	good_certificate = gdk_pixbuf_from_pixdata(&certificate_checked, FALSE, NULL);
}

void clear_certdata() {
	g_main_context_invoke(NULL, real_clear, NULL);
}
