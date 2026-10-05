#include "thread.h"
#include "framebuffer.h"
#include "colors.h"

static struct thread thread_table[THREAD_MAX];
static int thread_count = 0;
static struct thread *current_thread = 0;

/* Simple strlen we can use locally */
static int thread_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Copy up to max-1 characters, always NUL-terminate */
static void thread_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void thread_init(void)
{
    /* Zero the whole table. */
    for (int i = 0; i < THREAD_MAX; i++) {
        thread_table[i].rsp        = 0;
        thread_table[i].stack_base = 0;
        thread_table[i].stack_size = 0;
        thread_table[i].state      = THREAD_DEAD;
        thread_table[i].id         = i;
        thread_table[i].name[0]    = '\0';
    }

    /*
     * Register the currently-executing context as thread 0.
     *
     * rsp is 0 for now — it will be captured the first time a
     * context switch happens. For this session, nothing switches,
     * so thread 0 just sits there representing "the kernel main
     * path," which is what's running right now.
     */
    thread_table[0].state = THREAD_RUNNING;
    thread_strcpy(thread_table[0].name, "main", THREAD_NAME_MAX);

    thread_count = 1;
    current_thread = &thread_table[0];

    print_tagged("OK", COLOR_LIGHT_GREEN, "Initialized threads\n");

    print("[thread] current = ");
    print(current_thread->name);
    printc('\n');
}

void thread_create(const char *name, void (*fn)(void))
{
    if (thread_count >= THREAD_MAX) {
        print("[thread] table full, can't create ");
        print(name);
        printc('\n');
        return;
    }

    struct thread *t = &thread_table[thread_count];

    t->rsp        = 0;   /* set by context_switch on first schedule */
    t->stack_base = 0;
    t->stack_size = 0;
    t->state      = THREAD_READY;
    t->id         = thread_count;

    thread_strcpy(t->name, name, THREAD_NAME_MAX);

    (void)fn;   /* unused until we implement context_switch */

    print("[thread] created ");
    print(t->name);
    printc('\n');

    thread_count++;
}

void thread_yield(void)
{
    /*
     * Cooperative yield stub.
     *
     * Real implementation lands with context_switch.asm in the
     * next session. For now, this does nothing — callers that
     * call it just continue running.
     */
}

struct thread *thread_current(void)
{
    return current_thread;
}