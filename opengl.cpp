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
int window_width = 800, window_height = 300;

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
	if (height==0)								// Prevent A Divide By Zero By
	{
		height=1;							// Making Height Equal One
	}

  window_width, window_height = width, height;

	glViewport(0, 0, width, height);	
		glMatrixMode(GL_PROJECTION);						// Select The Projection Matrix
	glLoadIdentity();							// Reset The Projection Matrix

	// Calculate The Aspect Ratio Of The Window
	gluPerspective(45.0f,(GLfloat)width/(GLfloat)height,0.1f,100.0f);

	glMatrixMode(GL_MODELVIEW);						// Select The Modelview Matrix
	glLoadIdentity();							// Reset The Modelview Matrix
}

GLvoid InitGL(GLvoid)								// All Setup For OpenGL Goes Here
{
	glShadeModel(GL_FLAT);						
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);		
	glClearDepth(1.0f);							// Depth Buffer Setup
	glEnable(GL_DEPTH_TEST);						// Enables Depth Testing
	glDepthFunc(GL_LEQUAL);	
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);	
 }
 
GLvoid DrawGLScene(GLvoid)								// Here's Where We Do All The Drawing
{  
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);		// Clear The Screen And The Depth Buffer
	glLoadIdentity();
  // glTranslatef(-1.5f, 0.0f,-6.0f);       // Position   
  glTranslatef(ship.position.x, ship.position.y, -6.0f);
  glScalef( 0.2f, 0.2f, 0.2f);
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