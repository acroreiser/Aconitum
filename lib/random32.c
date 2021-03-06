/*
 * This is a maximally equidistributed combined Tausworthe generator
 * based on code from GNU Scientific Library 1.5 (30 Jun 2004)
 *
 * lfsr113 version:
 *
 * x_n = (s1_n ^ s2_n ^ s3_n ^ s4_n)
 *
 * s1_{n+1} = (((s1_n & 4294967294) << 18) ^ (((s1_n <<  6) ^ s1_n) >> 13))
 * s2_{n+1} = (((s2_n & 4294967288) <<  2) ^ (((s2_n <<  2) ^ s2_n) >> 27))
 * s3_{n+1} = (((s3_n & 4294967280) <<  7) ^ (((s3_n << 13) ^ s3_n) >> 21))
 * s4_{n+1} = (((s4_n & 4294967168) << 13) ^ (((s4_n <<  3) ^ s4_n) >> 12))
 *
 * The period of this generator is about 2^113 (see erratum paper).
 *
 * From: P. L'Ecuyer, "Maximally Equidistributed Combined Tausworthe
 * Generators", Mathematics of Computation, 65, 213 (1996), 203--213:
 * http://www.iro.umontreal.ca/~lecuyer/myftp/papers/tausme.ps
 * ftp://ftp.iro.umontreal.ca/pub/simulation/lecuyer/papers/tausme.ps
 *
 * There is an erratum in the paper "Tables of Maximally Equidistributed
 * Combined LFSR Generators", Mathematics of Computation, 68, 225 (1999),
 * 261--269: http://www.iro.umontreal.ca/~lecuyer/myftp/papers/tausme2.ps
 *
 *      ... the k_j most significant bits of z_j must be non-zero,
 *      for each j. (Note: this restriction also applies to the
 *      computer code given in [4], but was mistakenly not mentioned
 *      in that paper.)
 *
 * This affects the seeding procedure by imposing the requirement
 * s1 > 1, s2 > 7, s3 > 15, s4 > 127.
 */

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/random.h>
#include <linux/timer.h>

static DEFINE_PER_CPU(struct rnd_state, net_rand_state);

/**
 *	prandom_u32_state - seeded pseudo-random number generator.
 *	@state: pointer to state structure holding seeded state.
 *
 *	This is used for pseudo-randomness with no outside seeding.
 *	For more random results, use prandom_u32().
 */
u32 prandom_u32_state(struct rnd_state *state)
{
#define TAUSWORTHE(s,a,b,c,d) ((s&c)<<d) ^ (((s <<a) ^ s)>>b)

	state->s1 = TAUSWORTHE(state->s1,  6U, 13U, 4294967294U, 18U);
	state->s2 = TAUSWORTHE(state->s2,  2U, 27U, 4294967288U,  2U);
	state->s3 = TAUSWORTHE(state->s3, 13U, 21U, 4294967280U,  7U);
	state->s4 = TAUSWORTHE(state->s4,  3U, 12U, 4294967168U, 13U);

	return (state->s1 ^ state->s2 ^ state->s3 ^ state->s4);
}
EXPORT_SYMBOL(prandom_u32_state);

/**
 *	prandom_u32 - pseudo random number generator
 *
 *	A 32 bit pseudo-random number is generated using a fast
 *	algorithm suitable for simulation. This algorithm is NOT
 *	considered safe for cryptographic use.
 */
u32 prandom_u32(void)
{
	struct rnd_state *state = &get_cpu_var(net_rand_state);
	u32 res;

	res = prandom_u32_state(state);
	put_cpu_var(state);

	return res;
}
EXPORT_SYMBOL(prandom_u32);

/**
 *	prandom_bytes_state - get the requested number of pseudo-random bytes
 *
 *	@state: pointer to state structure holding seeded state.
 *	@buf: where to copy the pseudo-random bytes to
 *	@bytes: the requested number of bytes
 *
 *	This is used for pseudo-randomness with no outside seeding.
 *	For more random results, use prandom_bytes().
 */
void prandom_bytes_state(struct rnd_state *state, void *buf, int bytes)
{
	unsigned char *p = buf;
	int i;

	for (i = 0; i < round_down(bytes, sizeof(u32)); i += sizeof(u32)) {
		u32 random = prandom_u32_state(state);
		int j;

		for (j = 0; j < sizeof(u32); j++) {
			p[i + j] = random;
			random >>= BITS_PER_BYTE;
		}
	}
	if (i < bytes) {
		u32 random = prandom_u32_state(state);

		for (; i < bytes; i++) {
			p[i] = random;
			random >>= BITS_PER_BYTE;
		}
	}
}
EXPORT_SYMBOL(prandom_bytes_state);

/**
 *	prandom_bytes - get the requested number of pseudo-random bytes
 *	@buf: where to copy the pseudo-random bytes to
 *	@bytes: the requested number of bytes
 */
void prandom_bytes(void *buf, int bytes)
{
	struct rnd_state *state = &get_cpu_var(net_rand_state);

	prandom_bytes_state(state, buf, bytes);
	put_cpu_var(state);
}
EXPORT_SYMBOL(prandom_bytes);

static void prandom_warmup(struct rnd_state *state)
{
	/* Calling RNG ten times to satify recurrence condition */
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
}

/**
 *	prandom_seed - add entropy to pseudo random number generator
 *	@seed: seed value
 *
 *	Add some additional seeding to the prandom pool.
 */
void prandom_seed(u32 entropy)
{
	int i;
	/*
	 * No locking on the CPUs, but then somewhat random results are, well,
	 * expected.
	 */
	for_each_possible_cpu (i) {
		struct rnd_state *state = &per_cpu(net_rand_state, i);

		state->s1 = __seed(state->s1 ^ entropy, 2U);
		prandom_warmup(state);
	}
}
EXPORT_SYMBOL(prandom_seed);

/*
 *	Generate some initially weak seeding values to allow
 *	to start the prandom_u32() engine.
 */
static int __init prandom_init(void)
{
	int i;

	for_each_possible_cpu(i) {
		struct rnd_state *state = &per_cpu(net_rand_state,i);

#define LCG(x)	((x) * 69069U)	/* super-duper LCG */
		state->s1 = __seed(LCG((i + jiffies) ^ random_get_entropy()), 2U);
		state->s2 = __seed(LCG(state->s1),   8U);
		state->s3 = __seed(LCG(state->s2),  16U);
		state->s4 = __seed(LCG(state->s3), 128U);

		prandom_warmup(state);
	}

	return 0;
}
core_initcall(prandom_init);

static void __prandom_timer(unsigned long dontcare);
static DEFINE_TIMER(seed_timer, __prandom_timer, 0, 0);

static void __prandom_timer(unsigned long dontcare)
{
	u32 entropy;
	unsigned long expires;

	erandom_get_random_bytes((char *)&entropy, sizeof(entropy));
	prandom_seed(entropy);

	/* reseed every ~60 seconds, in [40 .. 80) interval with slack */
	expires = 40 + (prandom_u32() % 40);
	seed_timer.expires = jiffies + msecs_to_jiffies(expires * MSEC_PER_SEC);

	add_timer(&seed_timer);
}

static void __init __prandom_start_seed_timer(void)
{
	set_timer_slack(&seed_timer, HZ);
	seed_timer.expires = jiffies + msecs_to_jiffies(40 * MSEC_PER_SEC);
	add_timer(&seed_timer);
}

/*
 *	Generate better values after random number generator
 *	is fully initialized.
 */
static void __prandom_reseed(bool late)
{
	int i;
	unsigned long flags;
	static bool latch = false;
	static DEFINE_SPINLOCK(lock);

	/* Asking for random bytes might result in bytes getting
	 * moved into the nonblocking pool and thus marking it
	 * as initialized. In this case we would double back into
	 * this function and attempt to do a late reseed.
	 * Ignore the pointless attempt to reseed again if we're
	 * already waiting for bytes when the nonblocking pool
	 * got initialized.
	 */

	/* only allow initial seeding (late == false) once */
	if (!spin_trylock_irqsave(&lock, flags))
		return;

	if (latch && !late)
		goto out;

	latch = true;

	for_each_possible_cpu(i) {
		struct rnd_state *state = &per_cpu(net_rand_state,i);
		u32 seeds[4];

		erandom_get_random_bytes((char *)&seeds, sizeof(seeds));
		state->s1 = __seed(seeds[0],   2U);
		state->s2 = __seed(seeds[1],   8U);
		state->s3 = __seed(seeds[2],  16U);
		state->s4 = __seed(seeds[3], 128U);

		prandom_warmup(state);
	}
out:
	spin_unlock_irqrestore(&lock, flags);
}

void prandom_reseed_late(void)
{
	__prandom_reseed(true);
}

static int __init prandom_reseed(void)
{
	__prandom_reseed(false);
	__prandom_start_seed_timer();
	return 0;
}
late_initcall(prandom_reseed);
