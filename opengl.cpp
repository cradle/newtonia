#include <OpenGL/gl.h>		
#include <OpenGL/glu.h>		
#include <GLUT/glut.h>		

#include "main.cpp"
#include <iostream>

GLvoid InitGL(GLvoid);
GLvoid DrawGLScene(GLvoid);
GLvoid ReSizeGLScene(int Width, int Height);

int last_tick = glutGet(GLUT_ELAPSED_TIME);
Ship ship = Ship(0,0);
int window_width = 800, window_height = 600;

void tick(void) {
  int current_time = glutGet(GLUT_ELAPSED_TIME);
  
  ship.step(current_time - last_tick);
  
  last_tick = current_time;
  
  glutPostRedisplay();
}

void keyboard (unsigned char key, int x, int y) {
  switch(key) {
    case 27:
      exit(0);
		case 'a':
      ship.rotate_left();
      break;
    case 'd':
      ship.rotate_right();
      break;		
    case 'w':
      ship.thrust();
      break;
	}
}

void keyboard_up (unsigned char key, int x, int y) {
  
  switch(key) {
		case 'a':
      ship.rotate_left(false);
      break;
    case 'd':
      ship.rotate_right(false);
      break;		 
    case 'w':
      ship.thrust(false);
      break;
	}
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
  // glTranslatef(-1.5f, 0.0f,-6.0f);       // Position   
  //TODO: Refactor into a ship viewer
  //FIX: Ship disappearing bug
  int x = ((int(ship.position.x) + window_width/2) % window_width) - window_width/2;
  int y = ((int(ship.position.y) + window_height/2) % window_height) - window_height/2;
  glTranslatef(x, y, 0.0f);
  // glScalef( 1.0/window_width, 1.0/window_height, 0.1f);
  glScalef( ship.width, ship.height, 1.0f);
  	//TODO: Translate via position
  	//TODO: Rotate via heading
  glColor3f( 1.0f, 0.0f, 0.0f );

  glRotatef( ship.heading(), 0.0f, 0.0f, 1.0f);
  
	glBegin(GL_LINE_LOOP);						// Drawing The Ship
		glVertex3f( 0.0f, 1.0f, 0.0f);				// Top
		glVertex3f(-0.8f,-1.0f, 0.0f);				// Bottom Left
		glVertex3f( 0.0f,-0.5f, 0.0f);				// Bottom Middle
		glVertex3f( 0.8f,-1.0f, 0.0f);				// Bottom Right
	glEnd();							// Finished Drawing The Ship
	
	if(ship.thrusting) {
  	glBegin(GL_QUADS);						// Drawing The Flame
  		glVertex3f( 0.0f,-0.5f, 0.0f);				// Top 
  		glVertex3f(-0.4f,-0.75f, 0.0f);				// Left
  		glVertex3f( 0.0f,-1.5f, 0.0f);				// Bottom 
  		glVertex3f( 0.4f,-0.75f, 0.0f);				// Right
  	glEnd();							// Finished Drawing The Flame
	}
	
  glutSwapBuffers();
}