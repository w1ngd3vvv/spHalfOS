/* 3-D gears -- TinyGL demo for spHalfOS
 *
 * Based on the public-domain gears demo by Brian Paul (Mesa).
 * Ported to the spHalfOS TinyGL binding (GL/sphalfos.h).
 *
 * Mouse: drag with the left button to rotate the view.
 * Keys : arrows rotate, Z/z roll, space pause, F wireframe, ESC quit.
 */

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <GL/gl.h>
#include <GL/sphalfos.h>

#ifndef M_PI
#define M_PI 3.14159265
#endif

#define WIN_W 640
#define WIN_H 480

static GLfloat view_rotx = 20.0f, view_roty = 30.0f, view_rotz = 0.0f;
static GLint gear1, gear2, gear3;
static GLfloat angle = 0.0f;
static int paused;
static int wireframe;

static void gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
                 GLint teeth, GLfloat tooth_depth)
{
    GLint i;
    GLfloat r0, r1, r2;
    GLfloat da, u, v, len;

    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0f;
    r2 = outer_radius + tooth_depth / 2.0f;
    da = 2.0f * M_PI / teeth / 4.0f;

    glShadeModel(GL_FLAT);
    glNormal3f(0.0f, 0.0f, 1.0f);

    /* front face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        GLfloat a = i * 2.0f * M_PI / teeth;
        glVertex3f(r0 * cosf(a), r0 * sinf(a), width * 0.5f);
        glVertex3f(r1 * cosf(a), r1 * sinf(a), width * 0.5f);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), width * 0.5f);
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), width * 0.5f);
    }
    glEnd();

    /* front teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        GLfloat a = i * 2.0f * M_PI / teeth;
        glVertex3f(r1 * cosf(a), r1 * sinf(a), width * 0.5f);
        glVertex3f(r2 * cosf(a + da), r2 * sinf(a + da), width * 0.5f);
        glVertex3f(r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da), width * 0.5f);
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), width * 0.5f);
    }
    glEnd();

    glNormal3f(0.0f, 0.0f, -1.0f);

    /* back face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        GLfloat a = i * 2.0f * M_PI / teeth;
        glVertex3f(r1 * cosf(a), r1 * sinf(a), -width * 0.5f);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), -width * 0.5f);
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), -width * 0.5f);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), -width * 0.5f);
    }
    glEnd();

    /* back teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        GLfloat a = i * 2.0f * M_PI / teeth;
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), -width * 0.5f);
        glVertex3f(r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da), -width * 0.5f);
        glVertex3f(r2 * cosf(a + da), r2 * sinf(a + da), -width * 0.5f);
        glVertex3f(r1 * cosf(a), r1 * sinf(a), -width * 0.5f);
    }
    glEnd();

    /* outward faces of teeth */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i < teeth; i++) {
        GLfloat a = i * 2.0f * M_PI / teeth;
        glVertex3f(r1 * cosf(a), r1 * sinf(a), width * 0.5f);
        glVertex3f(r1 * cosf(a), r1 * sinf(a), -width * 0.5f);
        u = r2 * cosf(a + da) - r1 * cosf(a);
        v = r2 * sinf(a + da) - r1 * sinf(a);
        len = sqrtf(u * u + v * v);
        if (len > 0.0001f) { u /= len; v /= len; }
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r2 * cosf(a + da), r2 * sinf(a + da), width * 0.5f);
        glVertex3f(r2 * cosf(a + da), r2 * sinf(a + da), -width * 0.5f);
        glNormal3f(cosf(a), sinf(a), 0.0f);
        glVertex3f(r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da), width * 0.5f);
        glVertex3f(r2 * cosf(a + 2 * da), r2 * sinf(a + 2 * da), -width * 0.5f);
        u = r1 * cosf(a + 3 * da) - r2 * cosf(a + 2 * da);
        v = r1 * sinf(a + 3 * da) - r2 * sinf(a + 2 * da);
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), width * 0.5f);
        glVertex3f(r1 * cosf(a + 3 * da), r1 * sinf(a + 3 * da), -width * 0.5f);
        glNormal3f(cosf(a), sinf(a), 0.0f);
    }
    glVertex3f(r1 * cosf(0.0f), r1 * sinf(0.0f), width * 0.5f);
    glVertex3f(r1 * cosf(0.0f), r1 * sinf(0.0f), -width * 0.5f);
    glEnd();

    glShadeModel(GL_SMOOTH);

    /* inside radius cylinder */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        GLfloat a = i * 2.0f * M_PI / teeth;
        glNormal3f(-cosf(a), -sinf(a), 0.0f);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), -width * 0.5f);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), width * 0.5f);
    }
    glEnd();
}

static void reshape(int width, int height)
{
    GLfloat h = (GLfloat)height / (GLfloat)width;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0f, 1.0f, -h, h, 5.0f, 60.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -40.0f);
}

static void init_scene(void)
{
    static GLfloat pos[4] = { 5.0f, 5.0f, 10.0f, 0.0f };
    static GLfloat red[4] = { 0.8f, 0.1f, 0.0f, 1.0f };
    static GLfloat green[4] = { 0.0f, 0.8f, 0.2f, 1.0f };
    static GLfloat blue[4] = { 0.2f, 0.2f, 1.0f, 1.0f };
    static GLfloat white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);

    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);

    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
    gear1 = glGenLists(1);
    glNewList(gear1, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
    gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    glEndList();

    gear2 = glGenLists(1);
    glNewList(gear2, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
    gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    glEndList();

    gear3 = glGenLists(1);
    glNewList(gear3, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
    gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
    glEndList();

    (void)white;

    glEnable(GL_NORMALIZE);
}

static void draw(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
    glRotatef(view_rotx, 1.0f, 0.0f, 0.0f);
    glRotatef(view_roty, 0.0f, 1.0f, 0.0f);
    glRotatef(view_rotz, 0.0f, 0.0f, 1.0f);

    glPushMatrix();
    glTranslatef(-3.0f, -2.0f, 0.0f);
    glRotatef(angle, 0.0f, 0.0f, 1.0f);
    glCallList(gear1);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(3.1f, -2.0f, 0.0f);
    glRotatef(-2.0f * angle - 9.0f, 0.0f, 0.0f, 1.0f);
    glCallList(gear2);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-3.1f, 4.2f, 0.0f);
    glRotatef(-2.0f * angle - 25.0f, 0.0f, 0.0f, 1.0f);
    glCallList(gear3);
    glPopMatrix();

    glPopMatrix();
}

static int read_key(void)
{
    int c = tgl_key();
    if (c < 0) return -1;
    if (c != 27) return c;
    int c2 = tgl_key();
    if (c2 < 0) return 27;
    if (c2 != '[' && c2 != 'O') return 27;
    int c3 = tgl_key();
    if (c3 < 0) return 0;
    switch (c3) {
    case 'A': return 0x1001;
    case 'B': return 0x1002;
    case 'C': return 0x1003;
    case 'D': return 0x1004;
    default: return 0;
    }
}

int main(void)
{
    int mdown = 0, lmx = 0, lmy = 0;
    uint32_t last = 0;

    if (tgl_open(WIN_W, WIN_H) < 0) {
        printf("TinyGL: cannot create framebuffer\n");
        return 1;
    }
    reshape(tgl_width(), tgl_height());
    init_scene();

    last = tgl_ticks();

    for (;;) {
        uint32_t now = tgl_ticks();
        uint32_t dt = now - last;
        last = now;
        if (dt > 250) dt = 250;

        int k;
        while ((k = read_key()) >= 0) {
            if (k == 27) goto done;
            else if (k == ' ') paused = !paused;
            else if (k == 'f' || k == 'F') {
                wireframe = !wireframe;
                glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
            }
            else if (k > 0x1000) {
                switch (k) {
                case 0x1001: view_rotx += 5.0f; break;      /* up */
                case 0x1002: view_rotx -= 5.0f; break;      /* down */
                case 0x1003: view_roty += 5.0f; break;      /* right */
                case 0x1004: view_roty -= 5.0f; break;      /* left */
                }
            }
            else if (k == 'z') view_rotz += 5.0f;
            else if (k == 'Z') view_rotz -= 5.0f;
        }

        int mx, my, buttons;
        if (tgl_mouse(&mx, &my, &buttons) == 0) {
            int left = buttons & 1;
            if (left && mdown) {
                view_roty += (mx - lmx) * 0.5f;
                view_rotx += (my - lmy) * 0.5f;
            }
            mdown = left;
            lmx = mx;
            lmy = my;
        }

        if (!paused) angle += (GLfloat)dt * 0.06f;

        draw();
        tgl_swap();

        usleep(16000);
    }

done:
    tgl_close();
    return 0;
}
