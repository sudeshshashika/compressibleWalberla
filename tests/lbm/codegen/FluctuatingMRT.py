import sympy as sp
import pystencils as ps
from lbmpy.creationfunctions import create_lb_collision_rule
from lbmpy.moments import is_bulk_moment, is_shear_moment, get_order
from lbmpy.forcemodels import Guo
from lbmpy import LBMConfig, LBMOptimisation, LBStencil, Stencil, Method
from pystencils_walberla import CodeGeneration
from lbmpy_walberla import generate_lattice_model

with CodeGeneration() as ctx:
    data_type = "float64" if ctx.double_accuracy else "float32"

    omega_shear = sp.symbols("omega_shear")
    temperature = sp.symbols("temperature")
    force_field, vel_field = ps.fields(f"force(3), velocity(3): {data_type}[3D]", layout='fzyx')

    def rr_getter(moment_group):
        """Maps a group of moments to a relaxation rate (shear, bulk, even, odd)
        in the 4 relaxation time thermalized LB model
        """
        is_shear = [is_shear_moment(m, 3) for m in moment_group]
        is_bulk = [is_bulk_moment(m, 3) for m in moment_group]
        order = [get_order(m) for m in moment_group]
        assert min(order) == max(order)
        order = order[0]

        if order < 2:
            return [0.0] * len(moment_group)
        elif any(is_bulk):
            assert all(is_bulk)
            return [sp.Symbol("omega_bulk")] * len(moment_group)
        elif any(is_shear):
            assert all(is_shear)
            return [sp.Symbol("omega_shear")] * len(moment_group)
        elif order % 2 == 0:
            assert order > 2
            return [sp.Symbol("omega_even")] * len(moment_group)
        else:
            return [sp.Symbol("omega_odd")] * len(moment_group)


    fluctuating = {'temperature': temperature,
                   'block_offsets': 'walberla',
                   'rng_node': ps.rng.PhiloxTwoDoubles if ctx.double_accuracy else ps.rng.PhiloxFourFloats}

    lbm_config = LBMConfig(stencil=LBStencil(Stencil.D3Q19), method=Method.MRT, compressible=True,
                           weighted=True, zero_centered=False, relaxation_rates=rr_getter,
                           force_model=Guo(force=force_field.center_vector),
                           fluctuating=fluctuating)

    lbm_opt = LBMOptimisation(cse_global=True)

    collision_rule = create_lb_collision_rule(lbm_config=lbm_config, lbm_optimisation=lbm_opt)

    params = {}
    if ctx.optimize_for_localhost:
        params['cpu_vectorize_info'] = {'assume_inner_stride_one': True, 'assume_aligned': True}
    generate_lattice_model(ctx, 'FluctuatingMRT_LatticeModel', collision_rule, field_layout='fzyx',
                           data_type=data_type, **params)
