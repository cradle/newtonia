#include "glship.h"

#include <OpenGL/gl.h>		
#include <OpenGL/glu.h>		
#include <GLUT/glut.h>		

#include <iostream>

GLvoid InitGL(GLvoid);
GLvoid DrawGLScene(GLvoid);
GLvoid ReSizeGLScene(int Width, int Height);

int last_tick = glutGet(GLUT_ELAPSED_TIME);
GLShip ship = GLShip(0,0);
int window_width = 800, window_height = 600;

void tick(void) {
  int current_time = glutGet(GLUT_ELAPSED_TIME); 
  ship.step(current_time - last_tick);
  last_tick = current_time;
  glutPostRedisplay();
}

void keyboard (unsigned char key, int x, int y) {
  ship.input(key);
}
void keyboard_up (unsigned char key, int x, int y) {
  ship.input(key, false);
}

int main(int argc, char** argv)
{
	glutInit(&argc, argv);
	glutInitDisplayMode (GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
	glutInitWindowSize (window_width, window_height);
	glutInitWindowPosition (100, 100);
	glutCreateWindow (argv[0]);

	InitGL();

	glutDisplayFunc(DrawGLScene);
	glutReshapeFunc(ReSizeGLScene);
	glutIdleFunc(tick);
	glutKeyboardFunc(keyboard);
	glutKeyboardUpFunc(keyboard_up);

	glutMainLoop();

	return 0;
}

GLvoid ReSizeGLScene(int width, int height)				// Resize And Initialize The GL Window
{ 
  window_width = width;
  window_height = height;
  
  ship.resize(width, height);
  
  glViewport(0, 0, width, height);
  
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-width/2, width/2, -height/2, height/2, -1, 1);
  
  glMatrixMode(GL_MODELVIEW);  
  glLoadIdentity();

}

GLvoid InitGL(GLvoid)								// All Setup For OpenGL Goes Here
{
	glShadeModel(GL_FLAT);						
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);		
	glClearDepth(1.0f);							// Depth Buffer Setup
	glEnable(GL_DEPTH_TEST);						// Enables Depth Testing
	glDepthFunc(GL_LEQUAL);	
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);	
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
 }
 
GLvoid DrawGLScene(GLvoid)								// Here's Where We Do All The Drawing
{  
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);		// Clear The Screen And The Depth Buffer
	glLoadIdentity();

  ship.draw();
	
  glutSwapBuffers();
}