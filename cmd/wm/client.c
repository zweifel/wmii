/*
 * (C)opyright MMIV-MMV Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>

#include "wm.h"

#include <cext.h>

static Client zero_client = { 0 };

Client *alloc_client(Window w)
{
	static int id = 0;
	char buf[MAX_BUF];
	char buf2[MAX_BUF];
	Client *c = (Client *) emalloc(sizeof(Client));

	*c = zero_client;
	c->win = w;
	snprintf(buf, MAX_BUF, "/detached/c/%d", id);
	c->file[C_PREFIX] = ixp_create(ixps, buf);
	win_prop(dpy, c->win, XA_WM_NAME, buf2, MAX_BUF);
	snprintf(buf, MAX_BUF, "/detached/c/%d/name", id);
	c->file[C_NAME] = wmii_create_ixpfile(ixps, buf, buf2);
	id++;
	client = (Client **) attach_item_end((void **) client, c, sizeof(Client *));
	XSelectInput(dpy, c->win, CLIENT_MASK);
	return c;
}

void sel_client(Client * c, int raise, int up)
{
	Frame *f = 0;
	/* sel client */
	if (c) {
		f = c->frame;
		for (f->sel = 0; f->client && f->client[f->sel] != c; f->sel++);
		f->file[F_SEL_CLIENT]->content = c->file[C_PREFIX]->content;
		if (raise)
			XRaiseWindow(dpy, c->win);
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
	} else
		XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	invoke_wm_event(def[WM_EVENT_CLIENT_UPDATE]);
	if (up && f)
		sel_frame(f, raise, up, 0);
}

void set_client_state(Client * c, int state)
{
	long data[2];

	data[0] = (long) state;
	data[1] = (long) None;
	XChangeProperty(dpy, c->win, wm_state, wm_state, 32,
					PropModeReplace, (unsigned char *) data, 2);
}

void show_client(Client * c)
{
	XSelectInput(dpy, c->win, CLIENT_MASK & ~StructureNotifyMask);
	XMapWindow(dpy, c->win);
	XSelectInput(dpy, c->win, CLIENT_MASK);
	set_client_state(c, NormalState);
	grab_client(c, Mod1Mask, Button1);
	grab_client(c, Mod1Mask, Button3);
}

void hide_client(Client * c)
{
	ungrab_client(c, AnyModifier, AnyButton);
	XSelectInput(dpy, c->win, CLIENT_MASK & ~StructureNotifyMask);
	XUnmapWindow(dpy, c->win);
	XSelectInput(dpy, c->win, CLIENT_MASK);
	set_client_state(c, WithdrawnState);
}

void reparent_client(Client * c, Window w, int x, int y)
{
	XSelectInput(dpy, c->win, CLIENT_MASK & ~StructureNotifyMask);
	XReparentWindow(dpy, c->win, w, x, y);
	XSelectInput(dpy, c->win, CLIENT_MASK);
}

void grab_client(Client * c, unsigned long mod, unsigned int button)
{
	XSelectInput(dpy, c->win, CLIENT_MASK & ~StructureNotifyMask);
	XGrabButton(dpy, button, mod, c->win, False,
				ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
	if ((mod != AnyModifier) && num_lock_mask) {
		XGrabButton(dpy, button, mod | num_lock_mask, c->win,
					False, ButtonPressMask, GrabModeAsync, GrabModeAsync,
					None, None);
		XGrabButton(dpy, button, mod | num_lock_mask | LockMask,
					c->win, False, ButtonPressMask, GrabModeAsync,
					GrabModeAsync, None, None);
	}
	XSelectInput(dpy, c->win, CLIENT_MASK);
	XSync(dpy, False);
}

void ungrab_client(Client * c, unsigned long mod, unsigned int button)
{
	XSelectInput(dpy, c->win, CLIENT_MASK & ~StructureNotifyMask);
	XUngrabButton(dpy, button, mod, c->win);
	if (mod != AnyModifier && num_lock_mask) {
		XUngrabButton(dpy, button, mod | num_lock_mask, c->win);
		XUngrabButton(dpy, button, mod | num_lock_mask | LockMask, c->win);
	}
	XSelectInput(dpy, c->win, CLIENT_MASK);
	XSync(dpy, False);
}

void configure_client(Client * c)
{
	XConfigureEvent e;
	e.type = ConfigureNotify;
	e.event = c->win;
	e.window = c->win;
	e.x = c->rect.x;
	e.y = c->rect.y;
	if (c->frame) {
		e.x += c->frame->rect.x;
		e.y += c->frame->rect.y;
	}
	e.width = c->rect.width;
	e.height = c->rect.height;
	e.border_width = c->border;
	e.above = None;
	e.override_redirect = False;

	XSelectInput(dpy, c->win, CLIENT_MASK & ~StructureNotifyMask);
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *) & e);
	XSelectInput(dpy, c->win, CLIENT_MASK);
	XSync(dpy, False);
}

void close_client(Client * c)
{
	if (c->proto & PROTO_DEL)
		send_message(dpy, c->win, wm_protocols, wm_delete);
	else
		XKillClient(dpy, c->win);
}

void init_client(Client * c, XWindowAttributes * wa)
{
	long msize;
	c->rect.x = wa->x;
	c->rect.y = wa->y;
	c->border = wa->border_width;
	c->rect.width = wa->width + 2 * c->border;
	c->rect.height = wa->height + 2 * c->border;
	XSetWindowBorderWidth(dpy, c->win, 0);
	XSelectInput(dpy, c->win, PropertyChangeMask);
	c->proto = win_proto(c->win);
	XGetTransientForHint(dpy, c->win, &c->trans);
	/* size hints */
	if (!XGetWMNormalHints(dpy, c->win, &c->size, &msize)
		|| !c->size.flags)
		c->size.flags = PSize;
	XAddToSaveSet(dpy, c->win);
}

void handle_client_property(Client * c, XPropertyEvent * e)
{
	char buf[1024];
	long msize;

	buf[0] = '\0';
	if (e->state == PropertyDelete)
		return;					/* ignore */

	if (e->atom == wm_protocols) {
		/* update */
		c->proto = win_proto(c->win);
		return;
	}
	switch (e->atom) {
	case XA_WM_NAME:
		win_prop(dpy, c->win, XA_WM_NAME, buf, sizeof(buf));
		if (strlen(buf)) {
			if (c->file[C_NAME]->content)
				free(c->file[C_NAME]->content);
			c->file[C_NAME]->content = estrdup(buf);
			c->file[C_NAME]->size = strlen(buf);
		}
		if (c->frame)
			draw_client(c);
		invoke_wm_event(def[WM_EVENT_CLIENT_UPDATE]);
		break;
	case XA_WM_TRANSIENT_FOR:
		XGetTransientForHint(dpy, c->win, &c->trans);
		break;
	case XA_WM_NORMAL_HINTS:
		if (!XGetWMNormalHints(dpy, c->win, &c->size, &msize)
			|| !c->size.flags) {
			c->size.flags = PSize;
		}
		break;
	}
}

void destroy_client(Client * c)
{
	client = (Client **) detach_item((void **) client, c, sizeof(Client *));
	ixp_remove_file(ixps, c->file[C_PREFIX]);
	if (ixps->errstr)
		fprintf(stderr, "wmiiwm: destroy_client(): %s\n", ixps->errstr);
	free(c);
}

/* speed reasoned function for client property change */
void draw_client(Client * c)
{
	Frame *f = c->frame;
	unsigned int tabh = tab_height(f);
	int i, size;
	int tw;

	if (!tabh)
		return;
	size = count_items((void **) f->client);
	tw = f->rect.width;
	if (size)
		tw /= size;
	for (i = 0; f->client[i] && f->client[i] != c; i++);

	if (!f->client[i + 1])
		draw_tab(f, c->file[C_NAME]->content, i * tw, 0,
				 f->rect.width - (i * tw), tabh, ISSELFRAME(f)
				 && f->client[f->sel] == c);
	else
		draw_tab(f, c->file[C_NAME]->content, i * tw, 0, tw, tabh,
				 ISSELFRAME(f) && f->client[f->sel] == c);
}

void draw_clients(Frame * f)
{
	unsigned int tabh = tab_height(f);
	int i, size = count_items((void **) f->client);
	int tw = f->rect.width;

	if (!tabh || !size)
		return;
	if (size)
		tw /= size;
	for (i = 0; f->client[i]; i++) {
		if (!f->client[i + 1]) {
			int xoff = i * tw;
			draw_tab(f, f->client[i]->file[C_NAME]->content,
					 xoff, 0, f->rect.width - xoff, tabh, ISSELFRAME(f)
					 && f->client[f->sel] == f->client[i]);
			break;
		} else
			draw_tab(f, f->client[i]->file[C_NAME]->content,
					 i * tw, 0, tw, tabh, ISSELFRAME(f)
					 && f->client[f->sel] == f->client[i]);
	}
	XSync(dpy, False);
}

void gravitate(Client * c, unsigned int tabh, unsigned int bw, int invert)
{
	int dx = 0, dy = 0;
	int gravity = NorthWestGravity;

	if (c->size.flags & PWinGravity) {
		gravity = c->size.win_gravity;
	}
	/* y */
	switch (gravity) {
	case StaticGravity:
	case NorthWestGravity:
	case NorthGravity:
	case NorthEastGravity:
		dy = tabh;
		break;
	case EastGravity:
	case CenterGravity:
	case WestGravity:
		dy = -(c->rect.height / 2) + tabh;
		break;
	case SouthEastGravity:
	case SouthGravity:
	case SouthWestGravity:
		dy = -c->rect.height;
		break;
	default:					/* don't care */
		break;
	}

	/* x */
	switch (gravity) {
	case StaticGravity:
	case NorthWestGravity:
	case WestGravity:
	case SouthWestGravity:
		dx = bw;
		break;
	case NorthGravity:
	case CenterGravity:
	case SouthGravity:
		dx = -(c->rect.width / 2) + bw;
		break;
	case NorthEastGravity:
	case EastGravity:
	case SouthEastGravity:
		dx = -(c->rect.width + bw);
		break;
	default:					/* don't care */
		break;
	}

	if (invert) {
		dx = -dx;
		dy = -dy;
	}
	c->rect.x += dx;
	c->rect.y += dy;
}

void attach_client(Client * c)
{
	Area *a = 0;
	if (!page)
		alloc_page();
	/* transient stuff */
	a = SELAREA;
	if (c && c->trans) {
		Client *t = win_to_client(c->trans);
		if (t && t->frame)
			a = t->frame->area;
	}

	a->layout->attach(a, c);
	invoke_wm_event(def[WM_EVENT_PAGE_UPDATE]);
}
