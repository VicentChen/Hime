__exported import Utils.Sampling.SampleGenerator;

/** Returns a random number in [range.x, range.y).
    PDF = 1 / (range.y - range.x)
*/
void sampleRangeUniformly<S : ISampleGenerator>(const uint2 range, inout S sg, out uint idx, out float pdf)
{
    float delta = float(range.y) - float(range.x) + 1;
    float x = sampleNext1D(sg) * 0.999999f; // FIXME: hack to avoid x == 1
    idx = floor(range.x + x * delta);
    pdf = 1.f / delta;
}
