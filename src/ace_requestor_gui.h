#ifndef ACE_REQUESTOR_GUI_H
#define ACE_REQUESTOR_GUI_H

typedef struct _GtkWidget GtkWidget;

int ace_requestor_gui_start(const char *session, GtkWidget *parent);
void ace_requestor_gui_stop(void);

#endif
