/* 
 * The MIT License
 *
 * Copyright 2020 The OpenNARS authors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


int pX = 5;
int pY = 5;
#define DIRECTION_RIGHT '>'
#define DIRECTION_RIGHT_DOWN 'd'
#define DIRECTION_DOWN '^'
#define DIRECTION_LEFT_DOWN 'b'
#define DIRECTION_LEFT '<'
#define DIRECTION_LEFT_UP 'p'
#define DIRECTION_UP 'v'
#define DIRECTION_RIGHT_UP 'q'
char direction = DIRECTION_RIGHT; //right, right down, down, left down, left, left up, up, right up

//Angle transition via ^left operator
void NAR_Chamber_Left()
{
    if(direction == DIRECTION_RIGHT)
        direction = DIRECTION_RIGHT_UP;
    else
    if(direction == DIRECTION_RIGHT_UP)
        direction = DIRECTION_UP;
    else
    if(direction == DIRECTION_UP)
        direction = DIRECTION_LEFT_UP;
    else
    if(direction == DIRECTION_LEFT_UP)
        direction = DIRECTION_LEFT;
    else
    if(direction == DIRECTION_LEFT)
        direction = DIRECTION_LEFT_DOWN;
    else
    if(direction == DIRECTION_LEFT_DOWN)
        direction = DIRECTION_DOWN;
    else
    if(direction == DIRECTION_DOWN)
        direction = DIRECTION_RIGHT_DOWN;
    else
    if(direction == DIRECTION_RIGHT_DOWN)
        direction = DIRECTION_RIGHT;
}

//Angle transition via ^right operator
void NAR_Chamber_Right()
{
    if(direction == DIRECTION_RIGHT)
        direction = DIRECTION_RIGHT_DOWN;
    else
    if(direction == DIRECTION_RIGHT_DOWN)
        direction = DIRECTION_DOWN;
    else
    if(direction == DIRECTION_DOWN)
        direction = DIRECTION_LEFT_DOWN;
    else
    if(direction == DIRECTION_LEFT_DOWN)
        direction = DIRECTION_LEFT;
    else
    if(direction == DIRECTION_LEFT)
        direction = DIRECTION_LEFT_UP;
    else
    if(direction == DIRECTION_LEFT_UP)
        direction = DIRECTION_UP;
    else
    if(direction == DIRECTION_UP)
        direction = DIRECTION_RIGHT_UP;
    else
    if(direction == DIRECTION_RIGHT_UP)
        direction = DIRECTION_RIGHT;
}

//The world is composed of worldsizeX * worldsizeY cells
typedef struct
{
    bool wall;
    bool food;
}Cell;
#define worldsizeX 20
#define worldsizeY 10
Cell world[worldsizeX][worldsizeY] = {0};

//Draw cells
void Cell_Draw(Cell *cell)
{
    if(cell->wall)
    {
        fputs("#", stdout);
    }
    else
    {
        if(cell->food)
        {
            fputs("+", stdout);
        }
        else
        {
            fputs(" ", stdout);
        }
    }
}

char Cell_State(int i, int j)
{
    if(i < 0 || j < 0 || i >= worldsizeX || j >= worldsizeY)
    {
        return 'w';
    }
    Cell *cell = &world[i][j];
    if(cell->wall)
    {
        return 'w';
    }
    else
    {
        if(cell->food)
        {
            return 'f';
        }
        else
        {
            return 'o';
        }
    }
}

//Draw the world
void World_Draw()
{
    for(int i=0; i<worldsizeY; i++)
    {
        for(int j=0; j<worldsizeX; j++)
        {
            if(j == pX && i == pY)
            {
                char sdir[2] = { direction, 0 };
                fputs(sdir, stdout);
            }
            else
            {
                Cell_Draw(&world[j][i]);
            }
        }
        puts("");
    }
}

//A perception of the world
typedef struct
{
    int forward_pX; //position the agent reaches in case that
    int forward_pY; //moving forward is possible
    char viewfield[3];
    bool collision;
    bool reward;
    bool moved;
}Perception;

int irand(int n)
{
	int r, rmax = n*(RAND_MAX/n);
	while((r=rand()) >= rmax);
	return r / (RAND_MAX/n);
}

void spawnFood()
{
    int x, y;
    do
    {
        x = irand(worldsizeX);
        y = irand(worldsizeY);
        if(!world[x][y].wall && !world[x][y].food)
        {
            world[x][y].food = true;
            break;
        }
    }
    while(1);
}

Perception Agent_View()
{
    Perception ret = {0};
    if(direction == DIRECTION_RIGHT)
    {
        ret.forward_pX = pX + 1;
        ret.forward_pY = pY;
        ret.viewfield[0] = Cell_State(ret.forward_pX, ret.forward_pY+1);
        ret.viewfield[1] = Cell_State(ret.forward_pX, ret.forward_pY);
        ret.viewfield[2] = Cell_State(ret.forward_pX, ret.forward_pY-1);
    }
    if(direction == DIRECTION_RIGHT_UP)
    {
        ret.forward_pX = pX + 1;
        ret.forward_pY = pY + 1;
        ret.viewfield[0] = Cell_State(ret.forward_pX-1, ret.forward_pY);
        ret.viewfield[1] = Cell_State(ret.forward_pX,   ret.forward_pY);
        ret.viewfield[2] = Cell_State(ret.forward_pX,   ret.forward_pY+1);
    }
    if(direction == DIRECTION_UP)
    {
        ret.forward_pX = pX;
        ret.forward_pY = pY + 1;
        ret.viewfield[0] = Cell_State(ret.forward_pX-1, ret.forward_pY);
        ret.viewfield[1] = Cell_State(ret.forward_pX,   ret.forward_pY);
        ret.viewfield[2] = Cell_State(ret.forward_pX+1, ret.forward_pY);
    }
    if(direction == DIRECTION_LEFT_UP)
    {
        ret.forward_pX = pX - 1;
        ret.forward_pY = pY + 1;
        ret.viewfield[0] = Cell_State(ret.forward_pX,   ret.forward_pY-1);
        ret.viewfield[1] = Cell_State(ret.forward_pX,   ret.forward_pY);
        ret.viewfield[2] = Cell_State(ret.forward_pX+1, ret.forward_pY);
    }
    if(direction == DIRECTION_LEFT)
    {
        ret.forward_pX = pX - 1;
        ret.forward_pY = pY;
        ret.viewfield[0] = Cell_State(ret.forward_pX, ret.forward_pY-1);
        ret.viewfield[1] = Cell_State(ret.forward_pX, ret.forward_pY);
        ret.viewfield[2] = Cell_State(ret.forward_pX, ret.forward_pY+1);
    }
    if(direction == DIRECTION_LEFT_DOWN)
    {
        ret.forward_pX = pX - 1;
        ret.forward_pY = pY - 1;
        ret.viewfield[0] = Cell_State(ret.forward_pX+1, ret.forward_pY);
        ret.viewfield[1] = Cell_State(ret.forward_pX, ret.forward_pY);
        ret.viewfield[2] = Cell_State(ret.forward_pX, ret.forward_pY+1);
    }
    if(direction == DIRECTION_DOWN)
    {
        ret.forward_pX = pX;
        ret.forward_pY = pY - 1;
        ret.viewfield[0] = Cell_State(ret.forward_pX+1, ret.forward_pY);
        ret.viewfield[1] = Cell_State(ret.forward_pX, ret.forward_pY);
        ret.viewfield[2] = Cell_State(ret.forward_pX-1, ret.forward_pY);
    }
    if(direction == DIRECTION_RIGHT_DOWN)
    {
        ret.forward_pX = pX + 1;
        ret.forward_pY = pY - 1;
        ret.viewfield[0] = Cell_State(ret.forward_pX, ret.forward_pY+1);
        ret.viewfield[1] = Cell_State(ret.forward_pX, ret.forward_pY);
        ret.viewfield[2] = Cell_State(ret.forward_pX-1, ret.forward_pY);
    }
    bool outside = ret.forward_pX < 0 || ret.forward_pX >= worldsizeX || 
                   ret.forward_pY < 0 || ret.forward_pY >= worldsizeY;
    if(outside || world[ret.forward_pX][ret.forward_pY].wall)
    {
        ret.collision = true;
        ret.forward_pX = pX;
        ret.forward_pY = pY;
    }
    if(world[pX][pY].food)
    {
        ret.reward = true;
        world[pX][pY].food = false;
        spawnFood();
    }
    return ret;
}

//Forward move
void NAR_Chamber_Forward()
{
    Perception percept = Agent_View();
    //progress movement
    pX = percept.forward_pX;
    pY = percept.forward_pY;
}

void buildMaze(int x1, int y1, int x2, int y2)
{
    int w = x2-x1+1, h = y2-y1+1, finishedCount = 0; 
    int rw = (w+1)/2, rh = (h+1)/2;
    for (int i=1; i<rw*rh*1000 && finishedCount<rw*rh; i++)
    {
        int x = x1+2*irand(rw), y = y1+2*irand(rh);
        if(world[x][y].wall) continue;
        int dx = irand(2) == 1 ? (irand(2)*2-1) : 0;
        int dy = dx == 0       ? (irand(2)*2-1) : 0;
        int lx = x+dx*2, ly = y+dy*2;
        if (lx>=x1 && lx<=x2 && ly>=y1 && ly<=y2 && !world[lx][ly].wall)
        {
            world[x+dx][y+dy].wall = world[x][y].wall = true;
            finishedCount++;
        }
    }
}

int lastpX = 5;
int lastpY = 5;
void Agent_Invoke()
{
    Perception percept = Agent_View();
    char narsese[36] = "<(l_ * (m_ * r_)) --> percept>. :|:";
    narsese[3] = percept.viewfield[2];
    narsese[9] = percept.viewfield[1];
    narsese[14] = percept.viewfield[0];
    NAR_AddInputNarsese(narsese);
    if(percept.reward)
    {
        NAR_AddInputNarsese("eaten. :|:");
    }
    if(pX != lastpX || pY != lastpY)
    {
        NAR_AddInputNarsese("moved. :|:"); //innate drive to move (can also be learned)
    }
    lastpX = pX;
    lastpY = pY;
    //NAR_AddInputNarsese("eaten! :|:");
    NAR_AddInputNarsese("moved! :|:");
}

void NAR_Chamber(long iterations)
{
    NAR_INIT();
    puts(">>NAR Chamber start");
    NAR_AddOperation(Narsese_AtomicTerm("^left"), NAR_Chamber_Left); 
    NAR_AddOperation(Narsese_AtomicTerm("^right"), NAR_Chamber_Right); 
    NAR_AddOperation(Narsese_AtomicTerm("^forward"), NAR_Chamber_Forward);
    buildMaze(0, 1, worldsizeX, worldsizeY);
    //for(int i=0; i<20; i++) { spawnFood(); }
    long t=0;
    while(1)
    {
        t++;
        if(iterations != -1 && t++ > iterations)
        {
            exit(0);
        }
        fputs("\033[1;1H\033[2J", stdout); //POSIX clear screen
        World_Draw();
        Agent_Invoke();
        if(iterations == -1)
        {
            nanosleep((struct timespec[]){{0, 20000000L}}, NULL); //POSIX sleep
        }
    }
}